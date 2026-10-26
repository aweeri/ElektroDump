// ElektroDump — running statistics helpers.
#pragma once

#include <cstdint>
#include <vector>

namespace ed::stats {

// Welford's online mean/variance accumulator.
class RunningStats {
public:
    void add(double value);
    void reset();

    uint64_t count() const { return n_; }
    double mean() const { return n_ == 0 ? 0.0 : mean_; }
    double variance() const;  // sample variance (n-1)
    double stddev() const;
    double min() const { return n_ == 0 ? 0.0 : min_; }
    double max() const { return n_ == 0 ? 0.0 : max_; }

private:
    uint64_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
    double min_ = 0.0;
    double max_ = 0.0;
};

// Median of a copy of `values` (sorts internally). Returns 0 when empty.
double median(std::vector<double> values);

}  // namespace ed::stats
