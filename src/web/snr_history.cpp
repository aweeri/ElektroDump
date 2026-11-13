#include "web/snr_history.h"

#include <algorithm>

namespace ed::web {

void SnrHistory::push(float snr_db, int mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    SnrPoint point;
    point.snr_db = snr_db;
    point.mode = mode;

    // Running mean of the last five, computed before the push so a short ring
    // still averages what it has.
    float sum = snr_db;
    int count = 1;
    const std::size_t take = std::min<std::size_t>(4, ring_.size());
    for (std::size_t i = 0; i < take; ++i) {
        sum += ring_[ring_.size() - 1 - i].snr_db;
        ++count;
    }
    point.avg5_db = sum / static_cast<float>(count);

    ring_.push_back(point);
    while (ring_.size() > capacity_) {
        ring_.pop_front();
    }
}

std::vector<SnrPoint> SnrHistory::latest(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count == 0 || count > ring_.size()) {
        count = ring_.size();
    }
    const std::size_t begin = ring_.size() - count;
    return std::vector<SnrPoint>(ring_.begin() + static_cast<std::ptrdiff_t>(begin),
                                 ring_.end());
}

float SnrHistory::average(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.empty() || count == 0) {
        return 0.0f;
    }
    if (count > ring_.size()) {
        count = ring_.size();
    }
    float sum = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        sum += ring_[ring_.size() - 1 - i].snr_db;
    }
    return sum / static_cast<float>(count);
}

std::size_t SnrHistory::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ring_.size();
}

}  // namespace ed::web
