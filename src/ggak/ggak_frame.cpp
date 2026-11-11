#include "ggak/ggak_frame.h"

#include <chrono>

namespace ed::ggak {

namespace {

uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// TDB -> UNIX epoch plus the reference's Moscow-time offset; exposed as a config
// key in the pipeline so it can be corrected without a rebuild (PLAN.md D2).
constexpr double kGgakTimeBase = 10957.0 * 86400.0 - 3.0 * 3600.0;

// CCSDS attached sync marker that delimits a GGAK CADU (GobDump:
// ccsds_simple_psk_decoder, cadu_size 1792 bits, ASM included).
constexpr uint32_t kAsm = 0x1ACFFC1Du;

// Measured frame period is 1793 bits +/- jitter; search a window around the
// nominal 1792-bit period when hunting for the next ASM.
constexpr std::size_t kFrameBitsMin = 1784;
constexpr std::size_t kFrameBitsMax = 1804;

}  // namespace

bool ggak_asm_match(const uint8_t* raw) {
    return raw != nullptr && read_be32(raw) == kAsm;
}

uint16_t ggak_sum_crc(const uint8_t* raw) {
    if (raw == nullptr) {
        return 0;
    }
    uint16_t sum = 0;
    for (std::size_t i = 0; i < 222; ++i) {
        sum = static_cast<uint16_t>(sum + raw[i]);
    }
    return sum;
}

bool ggak_asm_at(const std::vector<uint8_t>& bits, std::size_t pos,
                 int polarity) {
    constexpr uint32_t kAsm = 0x1ACFFC1Du;
    if (pos + 32 > bits.size()) {
        return false;
    }
    uint32_t value = 0;
    for (std::size_t i = 0; i < 32; ++i) {
        const uint8_t bit =
            static_cast<uint8_t>(bits[pos + i] ^ (polarity ? 1 : 0));
        value = (value << 1) | bit;
    }
    return value == kAsm;
}

bool ggak_frame_crc_ok(const uint8_t* raw) {
    if (raw == nullptr) {
        return false;
    }
    const uint16_t expected =
        static_cast<uint16_t>((static_cast<uint16_t>(raw[222]) << 8) |
                              static_cast<uint16_t>(raw[223]));
    return ggak_sum_crc(raw) == expected;
}

void GgakFrameParser::reset() {
    bits_.clear();
    search_pos_ = 0;
    frame_start_ = 0;
    cand_start_ = 0;
    cand_polarity_ = 0;
    cand_frames_ = 0;
    cand_buf_.clear();
    locked_ = false;
    offset_ = 0;
    polarity_ = 0;
    fail_count_ = 0;
    frames_total_ = 0;
    frames_bad_ = 0;
}

bool GgakFrameParser::try_frame(std::size_t start, int polarity,
                                GgakFrame& out) const {
    if (start + kGgakFrameBits > bits_.size()) {
        return false;
    }
    for (std::size_t b = 0; b < kGgakFrameBytes; ++b) {
        uint8_t value = 0;
        for (int k = 0; k < 8; ++k) {
            const uint8_t bit =
                static_cast<uint8_t>(bits_[start + b * 8 + k] ^ polarity);
            value = static_cast<uint8_t>((value << 1) | bit);
        }
        out.raw[b] = value;
    }
    // Real GGAK framing (GobDump/SatDump `ccsds_simple_psk_decoder`, cadu_size
    // 1792 bits, RS off): a standard CCSDS attached sync marker delimits the
    // frame, and the 16-bit byte sum at [222..223] is only a quality check. The
    // sum alone matches random data about once in 65536 bit positions, which is
    // why syncing on it produced false frames; the 32-bit ASM does not.
    if (!ggak_asm_match(out.raw)) {
        return false;
    }
    out.crc_ok = ggak_frame_crc_ok(out.raw);
    // Header layout per GobDump's GGAKFrameHeader.
    out.frame_type = out.raw[4];
    out.id = out.raw[9];  // source_id
    out.frame_time = (static_cast<uint32_t>(out.raw[10]) << 8) |
                     static_cast<uint32_t>(out.raw[11]);  // coarse_time
    out.timestamp_unix =
        static_cast<double>(out.frame_time) + kGgakTimeBase;
    return true;
}

void GgakFrameParser::extract(std::vector<GgakFrame>& out) {
    // Frame period in the received bit stream is not exactly 1792 bits: measured
    // against a real Elektro-L signal the ASM repeats every 1793 bits (a clock
    // mismatch between the spacecraft and our sample rate). A deframer that
    // assumes a fixed 1792-bit period therefore walks off the ASM after a few
    // frames and never re-locks, which is what produced "tracking / no frames".
    // Hunt for the next ASM in a window around the nominal period instead.
    // A single call must make bounded progress: the outer loop can otherwise
    // spin when a pathological input keeps producing a candidate at the same
    // offset. Whatever is left is picked up by the next call.
    std::size_t iterations = 0;
    while (true) {
        if (++iterations > 20000) {
            break;
        }
        if (!locked_) {
            // Acquire on the 32-bit ASM, both polarities, at every bit offset.
            bool found = false;
            while (bits_.size() >= search_pos_ + kGgakFrameBits) {
                for (int polarity = 0; polarity < 2 && !found; ++polarity) {
                    if (ggak_asm_at(bits_, search_pos_, polarity)) {
                        cand_start_ = search_pos_;
                        cand_polarity_ = polarity;
                        found = true;
                    }
                }
                if (!found) {
                    ++search_pos_;
                }
            }
            if (!found) {
                if (search_pos_ > 4096) {
                    bits_.erase(bits_.begin(),
                                bits_.begin() +
                                    static_cast<std::ptrdiff_t>(search_pos_));
                    search_pos_ = 0;
                }
                return;
            }

            // Confirm with a second ASM one frame later (the window absorbs the
            // one-bit slip) before declaring lock.
            const std::size_t next =
                find_next_asm(cand_start_, cand_polarity_);
            if (next == 0) {
                if (bits_.size() < cand_start_ + kFrameBitsMax) {
                    return;  // wait for more bits
                }
                search_pos_ = cand_start_ + 1;  // false positive
                continue;
            }
            locked_ = true;
            polarity_ = cand_polarity_;
            frame_start_ = cand_start_;
            next_start_ = next;
            offset_ = static_cast<int>(cand_start_ % 8);
            fail_count_ = 0;
        }

        // Emit the frame at frame_start_, then move to the next ASM.
        if (bits_.size() < frame_start_ + kGgakFrameBits) {
            break;  // wait for more bits
        }
        GgakFrame frame;
        if (try_frame(frame_start_, polarity_, frame)) {
            ++frames_total_;
            fail_count_ = 0;
            out.push_back(frame);
        } else {
            ++frames_bad_;
            ++fail_count_;
        }

        if (next_start_ == 0) {
            next_start_ = find_next_asm(frame_start_, polarity_);
        }
        if (next_start_ == 0) {
            if (bits_.size() < frame_start_ + kFrameBitsMax) {
                break;  // wait for more bits
            }
            locked_ = false;
            search_pos_ = frame_start_ + 1;
            frame_start_ = 0;
            break;
        }
        frame_start_ = next_start_;
        next_start_ = 0;

        if (fail_count_ > 32) {
            locked_ = false;
            search_pos_ = frame_start_;
            fail_count_ = 0;
            frame_start_ = 0;
            break;
        }
    }

    // Drop consumed bits so the buffer stays bounded.
    if (frame_start_ > 4096) {
        bits_.erase(bits_.begin(),
                    bits_.begin() + static_cast<std::ptrdiff_t>(frame_start_));
        frame_start_ = 0;
        next_start_ = 0;
    }
}

std::size_t GgakFrameParser::find_next_asm(std::size_t from,
                                           int polarity) const {
    for (std::size_t off = kFrameBitsMin; off <= kFrameBitsMax; ++off) {
        const std::size_t pos = from + off;
        if (bits_.size() < pos + 32) {
            return 0;  // not enough bits yet
        }
        if (ggak_asm_at(bits_, pos, polarity)) {
            return pos;
        }
    }
    return 0;
}

void GgakFrameParser::push(const int8_t* soft, std::size_t n,
                           std::vector<GgakFrame>& out) {
    if (soft == nullptr || n == 0) {
        return;
    }
    bits_.reserve(bits_.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        bits_.push_back(soft[i] >= 0 ? 1 : 0);
    }
    extract(out);
}

}  // namespace ed::ggak
