#include "util/stats.h"

#include <algorithm>
#include <cmath>

namespace ed::stats {

void RunningStats::add(double value) {
    if (n_ == 0) {
        min_ = value;
        max_ = value;
    } else {
        min_ = std::min(min_, value);
        max_ = std::max(max_, value);
    }
    ++n_;
    const double delta = value - mean_;
    mean_ += delta / static_cast<double>(n_);
    m2_ += delta * (value - mean_);
}

void RunningStats::reset() {
    n_ = 0;
    mean_ = 0.0;
    m2_ = 0.0;
    min_ = 0.0;
    max_ = 0.0;
}

double RunningStats::variance() const {
    if (n_ < 2) {
        return 0.0;
    }
    return m2_ / static_cast<double>(n_ - 1);
}

double RunningStats::stddev() const {
    return std::sqrt(variance());
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double upper = values[mid];
    if (values.size() % 2 != 0) {
        return upper;
    }
    std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
    return (values[mid - 1] + upper) / 2.0;
}

}  // namespace ed::stats
