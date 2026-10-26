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

}  // namespace

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
    if (!ggak_frame_crc_ok(out.raw)) {
        return false;
    }
    out.id = out.raw[4];
    out.frame_time = read_be32(out.raw + 9);
    out.crc_ok = true;
    out.timestamp_unix =
        static_cast<double>(out.frame_time) + kGgakTimeBase;
    return true;
}

void GgakFrameParser::extract(std::vector<GgakFrame>& out) {
    if (!locked_) {
        // Acquisition: slide over the bit stream trying both polarities at every
        // start position. The sum-CRC is the only sync criterion, so the frame
        // start offset cannot be assumed.
        while (bits_.size() >= search_pos_ + kGgakFrameBits) {
            for (int polarity = 0; polarity < 2; ++polarity) {
                GgakFrame frame;
                if (try_frame(search_pos_, polarity, frame)) {
                    locked_ = true;
                    offset_ = static_cast<int>(search_pos_ % 8);
                    polarity_ = polarity;
                    frame_start_ = search_pos_ + kGgakFrameBits;
                    fail_count_ = 0;
                    ++frames_total_;
                    out.push_back(frame);
                    break;
                }
            }
            if (locked_) {
                break;
            }
            ++search_pos_;
        }

        if (!locked_) {
            // Drop bits that can no longer start a frame.
            if (search_pos_ > 4096) {
                bits_.erase(bits_.begin(),
                            bits_.begin() +
                                static_cast<std::ptrdiff_t>(search_pos_));
                search_pos_ = 0;
            }
            return;
        }
    }

    // Locked: consume whole frames, re-acquiring after a run of failures.
    while (bits_.size() >= frame_start_ + kGgakFrameBits) {
        GgakFrame frame;
        if (try_frame(frame_start_, polarity_, frame)) {
            ++frames_total_;
            fail_count_ = 0;
            out.push_back(frame);
        } else {
            ++frames_bad_;
            ++fail_count_;
            if (fail_count_ > 8) {
                locked_ = false;
                fail_count_ = 0;
                search_pos_ = frame_start_;
                break;
            }
        }
        frame_start_ += kGgakFrameBits;
    }

    // Drop consumed bits so the buffer stays bounded.
    if (frame_start_ > 4096) {
        bits_.erase(bits_.begin(),
                    bits_.begin() + static_cast<std::ptrdiff_t>(frame_start_));
        frame_start_ = 0;
    }
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
