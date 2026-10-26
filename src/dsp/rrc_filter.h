// ElektroDump — root-raised-cosine matched filter (PLAN.md D2/D3).
// Small FIR with a re-derived RRC response; taps are computed at configure time
// from the roll-off and samples/symbol. Keeps a history so it can be fed in blocks.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ed::dsp {

// Root-raised-cosine impulse response at time `t` (in symbol periods).
double rrc_impulse(double t, double alpha);

class RrcFilter {
public:
    // sps: samples/symbol at this filter's rate. alpha: roll-off (0..1).
    // taps: odd tap count (>= 3).
    void configure(double sps, double alpha, int taps = 33);
    void reset();

    // `out` must hold at least `n` samples. `out` may alias `in`.
    void process(const std::complex<float>* in, std::size_t n,
                 std::complex<float>* out);

    std::size_t taps() const { return taps_.size(); }
    std::size_t delay() const { return taps_.size() / 2; }

private:
    std::vector<float> taps_;
    std::vector<std::complex<float>> history_;
    std::size_t pos_ = 0;
};

}  // namespace ed::dsp
