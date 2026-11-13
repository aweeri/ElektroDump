// ElektroDump - one-second SNR history for the dashboard chart.
//
// The dashboard used to build its sparkline purely client-side, so every page
// load started from an empty axis. The daemon keeps the last N samples instead
// and hands them over on load, which also lets the chart show a running average.
#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace ed::web {

struct SnrPoint {
    float snr_db = 0.0f;
    float avg5_db = 0.0f;  // mean of this sample and the four before it
    int mode = 0;          // PipelineMode code at the time of the sample
};

class SnrHistory {
public:
    explicit SnrHistory(std::size_t capacity = 1800)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    void push(float snr_db, int mode);
    // Newest `count` samples, oldest first. Fewer are returned before the ring
    // fills.
    std::vector<SnrPoint> latest(std::size_t count) const;
    float average(std::size_t count) const;
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::deque<SnrPoint> ring_;
    std::size_t capacity_;
};

}  // namespace ed::web
