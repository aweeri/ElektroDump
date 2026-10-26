#include "ggak/ggak_science.h"

#include <cmath>

#include "ggak/ggak_skl.h"

namespace ed::ggak {

namespace {

// parse_skl marks the absent slot 10 with a large negative sentinel.
bool value_present(double value) {
    return std::isfinite(value) && value > -1.0e90;
}

std::string channel_name(int index) {
    // Physical channel names are not yet mapped; use stable generic labels.
    std::string name = "SKL-";
    if (index < 10) {
        name += "0";
    }
    name += std::to_string(index);
    return name;
}

}  // namespace

void GgakScienceStore::configure(std::size_t max_points_per_channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_points_ = max_points_per_channel > 0 ? max_points_per_channel : 3600;
    for (int i = 0; i < kGgakSklChannels; ++i) {
        series_[i].clear();
        latest_[i] = ChannelLatest{};
    }
    stats_ = Stats{};
}

void GgakScienceStore::add_frame(const GgakFrame& frame) {
    const std::vector<GgakSklRecord> records = parse_skl(frame);

    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.frames;
    if (records.empty()) {
        return;
    }
    ++stats_.skl_frames;

    for (const GgakSklRecord& record : records) {
        ++stats_.records;
        for (int c = 0; c < kGgakSklChannels; ++c) {
            const double value = record.dat[c];
            if (!value_present(value)) {
                continue;
            }
            GgakSample sample;
            sample.time = record.time;
            sample.value = value;
            sample.crc_ok = frame.crc_ok;
            std::deque<GgakSample>& series = series_[c];
            if (series.size() >= max_points_) {
                series.pop_front();
            }
            series.push_back(sample);
            latest_[c].has_value = true;
            latest_[c].time = sample.time;
            latest_[c].value = value;
        }
    }
}

std::vector<GgakChannelInfo> GgakScienceStore::channels() const {
    std::vector<GgakChannelInfo> out;
    out.reserve(kGgakSklChannels);
    for (int i = 0; i < kGgakSklChannels; ++i) {
        GgakChannelInfo info;
        info.id = i;
        info.name = channel_name(i);
        info.unit = "counts";
        info.present = i != 10;
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<GgakScienceStore::ChannelLatest> GgakScienceStore::latest()
    const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ChannelLatest> out(kGgakSklChannels);
    for (int i = 0; i < kGgakSklChannels; ++i) {
        out[i] = latest_[i];
    }
    return out;
}

bool GgakScienceStore::series(int channel, std::size_t max_points,
                              std::vector<GgakSample>& out) const {
    out.clear();
    if (channel < 0 || channel >= kGgakSklChannels) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::deque<GgakSample>& series = series_[channel];
    const std::size_t total = series.size();
    const std::size_t count = max_points == 0 ? total
                                              : (max_points < total ? max_points
                                                                    : total);
    const std::size_t skip = total - count;
    out.reserve(count);
    for (std::size_t i = skip; i < total; ++i) {
        out.push_back(series[i]);
    }
    return true;
}

std::size_t GgakScienceStore::sample_count(int channel) const {
    if (channel < 0 || channel >= kGgakSklChannels) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return series_[channel].size();
}

bool GgakScienceStore::has_data() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < kGgakSklChannels; ++i) {
        if (!series_[i].empty()) {
            return true;
        }
    }
    return false;
}

GgakScienceStore::Stats GgakScienceStore::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace ed::ggak
