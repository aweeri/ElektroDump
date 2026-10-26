// ElektroDump — fractional symbol-timing recovery (PLAN.md D3.3).
// Gardner detector with linear interpolation, so it works at a non-integer
// samples/symbol rate (LRIT: 3.40). Symbol and midpoint instants are tracked
// explicitly and initialised to the matched filter's group delay, avoiding the
// unstable midpoint lock.
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ed::dsp {

class GardnerTiming {
public:
    // initial_delay: samples before the first symbol instant (the matched
    // filter's group delay).
    void configure(double samples_per_symbol, double phase_gain = 0.05,
                   double period_gain = 0.0005, double initial_delay = 0.0);
    void reset();

    void process(const std::complex<float>* in, std::size_t n,
                 std::vector<std::complex<float>>& out);

    double samples_per_symbol() const { return period_; }

private:
    std::complex<float> interpolate(double t) const;

    double nominal_ = 1.0;
    double period_ = 1.0;
    double phase_gain_ = 0.05;
    double period_gain_ = 0.0005;
    double initial_delay_ = 0.0;

    double next_sym_ = 0.0;
    double next_mid_ = 0.0;
    std::uint64_t index_ = 0;  // index of the newest input sample

    std::complex<float> s0_{0.0f, 0.0f};  // newest input sample
    std::complex<float> s1_{0.0f, 0.0f};  // previous input sample
    std::complex<float> prev_sym_{0.0f, 0.0f};
    std::complex<float> mid_{0.0f, 0.0f};
    bool have_prev_ = false;
    bool have_mid_ = false;
};

}  // namespace ed::dsp
