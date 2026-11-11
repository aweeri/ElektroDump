// ElektroDump — GGAK 224-byte frame + sum-CRC + frame synchroniser (PLAN.md D2).
// GGAK has no CCSDS ASM and no Reed-Solomon: a frame is exactly 224 bytes,
// validated by a 16-bit sum of bytes 0..221 against bytes 222..223 (big-endian).
// The parser uses the sum-CRC itself as the sync criterion, trying all 8 bit
// offsets and both polarities until it locks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ed::ggak {

inline constexpr std::size_t kGgakFrameBytes = 224;
inline constexpr std::size_t kGgakFrameBits = kGgakFrameBytes * 8;  // 1792

struct GgakFrame {
    uint8_t raw[kGgakFrameBytes] = {0};
    uint8_t id = 0;               // raw[4]; 48 == SKL
    uint32_t frame_time = 0;      // raw[9..12], big-endian
    bool crc_ok = false;
    double timestamp_unix = 0.0;  // frame_time + TDB->UNIX + MSK offset
};

// 16-bit sum of bytes 0..221 (wraps at 16 bits, matching the reference).
uint16_t ggak_sum_crc(const uint8_t* raw);

bool ggak_frame_crc_ok(const uint8_t* raw);

// Streaming frame synchroniser: feed soft symbols, it appends complete
// CRC-validated frames to `out`, re-acquiring after a run of bad frames.
class GgakFrameParser {
public:
    void reset();

    void push(const int8_t* soft, std::size_t n,
              std::vector<GgakFrame>& out);

    bool locked() const { return locked_; }
    uint64_t frames_total() const { return frames_total_; }
    uint64_t frames_bad() const { return frames_bad_; }
    int bit_offset() const { return offset_; }
    int polarity() const { return polarity_; }

private:
    bool try_frame(std::size_t start, int polarity, GgakFrame& out) const;
    void extract(std::vector<GgakFrame>& out);

    std::vector<uint8_t> bits_;
    std::size_t search_pos_ = 0;   // next acquisition start position
    std::size_t frame_start_ = 0;  // next frame start once locked
    // A single 16-bit byte-sum matches random data roughly once every 65536 bit
    // positions, so a lone match is not evidence of a frame: a candidate start
    // must be confirmed by consecutive matches before anything is emitted.
    static constexpr std::size_t kConfirmFrames = 3;
    std::size_t cand_start_ = 0;
    int cand_polarity_ = 0;
    std::size_t cand_frames_ = 0;
    std::vector<GgakFrame> cand_buf_;  // candidates awaiting confirmation
    bool locked_ = false;
    int offset_ = 0;
    int polarity_ = 0;
    int fail_count_ = 0;
    uint64_t frames_total_ = 0;
    uint64_t frames_bad_ = 0;
};

}  // namespace ed::ggak
