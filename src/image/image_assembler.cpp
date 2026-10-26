#include "image/image_assembler.h"

#include <algorithm>
#include <cstddef>
#include <ctime>

#include "core/config.h"
#include "core/time_util.h"

namespace ed::image {

namespace {

// CCSDS epoch (1958-01-01) to the Unix epoch, in days.
constexpr int64_t kCcsdsEpochDays = 4383;

std::string timestamp_from(const xrit::XritFile& file) {
    if (!file.has_header(xrit::TimeStampRecord::kType)) {
        return {};
    }
    const xrit::TimeStampRecord stamp(
        file.data.data() +
        file.header_offsets.at(xrit::TimeStampRecord::kType));
    const int64_t unix_seconds =
        static_cast<int64_t>(stamp.days) * 86400 +
        static_cast<int64_t>(stamp.milliseconds_of_day) / 1000 -
        kCcsdsEpochDays * 86400;
    const std::time_t seconds = static_cast<std::time_t>(unix_seconds);
    return time::iso8601(time::Clock::from_time_t(seconds), false);
}

}  // namespace

ImageAssembler::ImageAssembler(const Config& cfg) : config_(&cfg) {}

void ImageAssembler::reset() {
    wip_.clear();
    seen_.clear();
    stats_ = Stats{};
}

void ImageAssembler::finalize(AssembledChannel& channel, AssembledChannel& out) {
    const int missing = channel.segments_expected - channel.segments_present;
    if (missing > 0) {
        stats_.segments_missing += static_cast<uint64_t>(missing);
    }
    const double ratio =
        channel.segments_expected > 0
            ? static_cast<double>(missing) /
                  static_cast<double>(channel.segments_expected)
            : 0.0;
    const double max_ratio =
        config_ != nullptr
            ? config_->get_double("image.max_missing_segment_ratio", 0.05)
            : 0.05;
    channel.partial = ratio > max_ratio;
    out = std::move(channel);
}

bool ImageAssembler::push(const xrit::XritFile& file, AssembledChannel& out) {
    if (!file.header_parsed || !file.has_header(xrit::PrimaryHeader::kType)) {
        return false;
    }
    const xrit::PrimaryHeader primary = file.primary();
    if (primary.file_type_code != 0) {
        return false;
    }
    if (!file.has_header(xrit::SegmentIdentificationRecord::kType) ||
        !file.has_header(xrit::ImageStructureRecord::kType)) {
        return false;
    }

    const xrit::SegmentIdentificationRecord segment =
        file.segment_identification();
    const xrit::ImageStructureRecord structure = file.image_structure();
    const Key key(segment.sc_id, segment.channel_id);

    auto it = wip_.find(key);
    if (it == wip_.end()) {
        AssembledChannel channel;
        channel.channel_id = segment.channel_id;
        channel.sc_id = segment.sc_id;
        channel.structure = structure;
        channel.image.resize(structure.columns_count, structure.lines_count, 1);
        channel.valid.resize(structure.columns_count, structure.lines_count);
        channel.segments_expected =
            static_cast<int>(segment.planned_end_segment) -
            static_cast<int>(segment.planned_start_segment) + 1;
        if (channel.segments_expected < 1) {
            channel.segments_expected = 1;
        }
        if (file.has_header(xrit::ImageNavigationRecord::kType)) {
            channel.navigation = xrit::ImageNavigationRecord(
                file.data.data() +
                file.header_offsets.at(xrit::ImageNavigationRecord::kType));
            channel.has_navigation = true;
        }
        channel.timestamp_utc = timestamp_from(file);
        it = wip_.emplace(key, std::move(channel)).first;
        seen_[key] = std::vector<bool>(
            static_cast<std::size_t>(it->second.segments_expected), false);
    }

    AssembledChannel& channel = it->second;
    std::vector<bool>& seen = seen_[key];
    const int expected = channel.segments_expected;

    int segment_index = static_cast<int>(segment.segment_sequence_number) -
                        static_cast<int>(segment.planned_start_segment);
    if (segment_index < 0) {
        segment_index = 0;
    }
    if (segment_index >= expected) {
        segment_index = expected - 1;
    }

    int segment_height = 0;
    const int64_t override_height =
        config_ != nullptr ? config_->get_int("image.segment_height", 0) : 0;
    if (override_height > 0) {
        segment_height = static_cast<int>(override_height);
    } else {
        segment_height = (channel.image.height + expected - 1) / expected;
    }
    if (segment_height < 1) {
        segment_height = 1;
    }

    const int row_offset = segment_index * segment_height;
    const int bits_per_pixel = structure.bits_per_pixel;
    const int bytes_per_sample = bits_per_pixel <= 8 ? 1 : 2;
    const int max_value =
        bits_per_pixel >= 16 ? 65535 : ((1 << bits_per_pixel) - 1);

    const std::size_t data_offset = file.total_header_length;
    if (data_offset >= file.data.size()) {
        return false;
    }
    const uint8_t* data = file.data.data() + data_offset;
    const std::size_t data_length = file.data.size() - data_offset;

    const int rows = std::min(segment_height, channel.image.height - row_offset);
    const int columns = std::min<int>(structure.columns_count, channel.image.width);
    for (int row = 0; row < rows; ++row) {
        const int y = row_offset + row;
        for (int x = 0; x < columns; ++x) {
            const std::size_t sample_index =
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(structure.columns_count) +
                static_cast<std::size_t>(x);
            const std::size_t byte_index =
                sample_index * static_cast<std::size_t>(bytes_per_sample);
            if (byte_index + static_cast<std::size_t>(bytes_per_sample) >
                data_length) {
                break;
            }
            uint16_t value = 0;
            if (bytes_per_sample == 1) {
                value = data[byte_index];
            } else {
                value = static_cast<uint16_t>(
                    (static_cast<uint16_t>(data[byte_index]) << 8) |
                    data[byte_index + 1]);
            }
            if (value > max_value) {
                value = static_cast<uint16_t>(max_value);
            }
            const std::size_t pixel =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(channel.image.width) +
                static_cast<std::size_t>(x);
            channel.image.data[pixel] = value;
            channel.valid.data[pixel] = 1;
        }
    }

    if (!seen[static_cast<std::size_t>(segment_index)]) {
        seen[static_cast<std::size_t>(segment_index)] = true;
        ++channel.segments_present;
        ++stats_.segments_placed;
    }
    ++stats_.files;

    if (channel.segments_present >= channel.segments_expected) {
        finalize(channel, out);
        wip_.erase(it);
        seen_.erase(key);
        ++stats_.channels;
        return true;
    }
    return false;
}

void ImageAssembler::flush(std::vector<AssembledChannel>& out) {
    for (auto& entry : wip_) {
        AssembledChannel channel;
        finalize(entry.second, channel);
        out.push_back(std::move(channel));
        ++stats_.channels;
    }
    wip_.clear();
    seen_.clear();
}

}  // namespace ed::image
