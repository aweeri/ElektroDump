// ElektroDump — in-memory GGAK SKL science store (M9).
// Bounded, thread-safe recent window of de-quantised SKL channel series, fed by
// the GGAK pipeline and read by /api/v1/ggak*. With no SKL frames seen,
// has_data() is false and the endpoints return empty results, not fabricated ones.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "ggak/ggak_frame.h"

namespace ed::ggak {

// SKL carries 12 de-quantised slots; slot 10 is not present in the downlink.
inline constexpr int kGgakSklChannels = 12;

struct GgakChannelInfo {
    int id = 0;
    std::string name;  // generic label; physical names are not yet mapped
    std::string unit;  // de-quantised engineering counts
    bool present = true;  // false for slot 10 (absent in the downlink)
};

struct GgakSample {
    double time = 0.0;  // unix seconds
    double value = 0.0;
    bool crc_ok = false;
};

class GgakScienceStore {
public:
    struct Stats {
        uint64_t frames = 0;      // all validated frames
        uint64_t skl_frames = 0;  // id == 48 frames
        uint64_t records = 0;     // SKL records ingested
    };

    struct ChannelLatest {
        bool has_value = false;
        double time = 0.0;
        double value = 0.0;
    };

    void configure(std::size_t max_points_per_channel = 3600);

    // Ingest one validated frame; only id == 48 frames contribute series.
    void add_frame(const GgakFrame& frame);

    // Static channel metadata (kGgakSklChannels entries).
    std::vector<GgakChannelInfo> channels() const;

    // Latest value per channel; `has_value` is false when nothing was seen.
    std::vector<ChannelLatest> latest() const;

    // Copy up to `max_points` recent samples of `channel` (0-based), oldest
    // first; false for an invalid channel.
    bool series(int channel, std::size_t max_points,
                std::vector<GgakSample>& out) const;

    std::size_t sample_count(int channel) const;

    bool has_data() const;

    Stats stats() const;

private:
    mutable std::mutex mutex_;
    std::size_t max_points_ = 3600;
    std::deque<GgakSample> series_[kGgakSklChannels];
    ChannelLatest latest_[kGgakSklChannels];
    Stats stats_;
};

}  // namespace ed::ggak
