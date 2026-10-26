// ElektroDump — OQPSK I/Q offset handling (PLAN.md D3.3, milestone M5).
// OQPSK delays the Q rail by half a symbol; delaying I by T/2 turns it into
// ordinary QPSK the shared Costas/Gardner stages process unchanged. The delay is
// fractional (HRIT: 5.217 samples/symbol, T/2 = 2.609), so I is resampled with
// linear interpolation.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ed::dsp {

class OqpskAligner {
public:
    // samples_per_symbol: the demod's samples/symbol (T); I is delayed by T/2.
    void configure(double samples_per_symbol);
    void reset();

    // `out` may alias `in`.
    void process(const std::complex<float>* in, std::size_t n,
                 std::complex<float>* out);

    double delay_samples() const { return delay_; }

private:
    double delay_ = 0.0;
    int int_delay_ = 0;
    double frac_ = 0.0;
    std::vector<float> i_hist_;  // circular history of the I rail
    std::size_t pos_ = 0;
};

}  // namespace ed::dsp
