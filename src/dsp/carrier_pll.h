// ElektroDump — second-order carrier tracking PLL (PLAN.md D2).
// Removes the residual carrier offset after SDR downconversion. Standard
// critically-damped 2nd-order loop; the phase detector is atan2(Q, I), which
// locks to the dominant carrier of a PM signal.
#pragma once

#include <complex>
#include <cstddef>

namespace ed::dsp {

class CarrierPll {
public:
    // loop_bw is normalised (0..1). max_offset_hz bounds the tracked offset.
    void configure(double loop_bw, double max_offset_hz, double sample_rate);
    void reset();

    // In-place-safe: `out` may alias `in`.
    void process(const std::complex<float>* in, std::size_t n,
                 std::complex<float>* out);

    // Current tracked offset in Hz (for diagnostics).
    double freq_hz() const;

private:
    double alpha_ = 0.0;
    double beta_ = 0.0;
    double phase_ = 0.0;
    double freq_ = 0.0;
    double max_freq_ = 0.0;
    double sample_rate_ = 1.0;
};

}  // namespace ed::dsp
