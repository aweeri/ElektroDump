// ElektroDump — symbol-timing recovery (PLAN.md D2/D3).
// Block-based maximum-eye recovery: matched-filter output is accumulated per
// sampling phase and the phase with the largest mean |I| is the symbol instant.
// Robust on the narrowband GGAK signal (a Gardner detector is confused by the
// alternating preamble) and tracks slow clock drift as the phase is re-estimated.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ed::dsp {

class SymbolTimingRecovery {
public:
    // sps: nominal samples per symbol. block_symbols: symbols per phase update.
    void configure(double sps, int block_symbols = 64);
    void reset();

    void process(const std::complex<float>* in, std::size_t n,
                 std::vector<std::complex<float>>& out);

    double sps() const { return sps_; }
    int phase() const { return phase_; }

private:
    double sps_ = 1.0;
    int sps_int_ = 1;
    int phase_ = 0;
    int block_symbols_ = 64;
    std::size_t sample_index_ = 0;
    std::size_t block_samples_ = 0;
    std::vector<double> eye_;
};

}  // namespace ed::dsp
