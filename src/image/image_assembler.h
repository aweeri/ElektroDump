// ElektroDump — xRIT segments -> full-disk Image16 per channel (spec §1.3).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "image/image.h"
#include "xrit/xrit_file.h"
#include "xrit/xrit_headers.h"

namespace ed {
class Config;
}

namespace ed::image {

struct AssembledChannel {
    uint8_t channel_id = 0;
    uint16_t sc_id = 0;
    Image16 image;  // raw counts, one channel
    Mask valid;     // 0 where a segment was missing
    int segments_expected = 0;
    int segments_present = 0;
    bool partial = false;  // missing ratio > image.max_missing_segment_ratio
    xrit::ImageStructureRecord structure;
    xrit::ImageNavigationRecord navigation;
    bool has_navigation = false;
    std::string timestamp_utc;  // from TimeStampRecord, ISO-8601
};

class ImageAssembler {
public:
    explicit ImageAssembler(const Config& cfg);

    // Feed one finalized xRIT file; true when a channel is complete (all planned
    // segments seen) or when flush() is called.
    bool push(const xrit::XritFile& file, AssembledChannel& out);

    // Emit any in-progress channels (called at capture end).
    void flush(std::vector<AssembledChannel>& out);

    void reset();

    struct Stats {
        uint64_t files = 0;
        uint64_t segments_placed = 0;
        uint64_t segments_missing = 0;
        uint64_t channels = 0;
    };
    Stats stats() const { return stats_; }

private:
    using Key = std::pair<uint16_t, uint8_t>;

    void finalize(AssembledChannel& channel, AssembledChannel& out);

    const Config* config_ = nullptr;
    std::map<Key, AssembledChannel> wip_;
    std::map<Key, std::vector<bool>> seen_;
    Stats stats_;
};

}  // namespace ed::image
