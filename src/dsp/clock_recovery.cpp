#include "dsp/clock_recovery.h"

#include <algorithm>
#include <cmath>

namespace ed::dsp {

void SymbolTimingRecovery::configure(double sps, int block_symbols) {
    if (!(sps > 1.0)) {
        sps = 2.0;
    }
    sps_ = sps;
    sps_int_ = static_cast<int>(std::lround(sps));
    if (sps_int_ < 1) {
        sps_int_ = 1;
    }
    block_symbols_ = block_symbols > 0 ? block_symbols : 64;
    reset();
}

void SymbolTimingRecovery::reset() {
    phase_ = 0;
    sample_index_ = 0;
    block_samples_ = 0;
    eye_.assign(static_cast<std::size_t>(sps_int_), 0.0);
}

void SymbolTimingRecovery::process(const std::complex<float>* in, std::size_t n,
                                   std::vector<std::complex<float>>& out) {
    if (in == nullptr || sps_int_ < 1) {
        return;
    }
    const std::size_t sps = static_cast<std::size_t>(sps_int_);
    const std::size_t block_len =
        sps * static_cast<std::size_t>(block_symbols_);

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t p = sample_index_ % sps;
        // Accumulate the eye opening for this phase.
        eye_[p] += std::fabs(static_cast<double>(in[i].real()));

        if (static_cast<int>(p) == phase_) {
            out.push_back(in[i]);
        }

        ++sample_index_;
        ++block_samples_;

        if (block_samples_ >= block_len) {
            // Pick the phase with the largest mean eye opening.
            std::size_t best = 0;
            for (std::size_t k = 1; k < eye_.size(); ++k) {
                if (eye_[k] > eye_[best]) {
                    best = k;
                }
            }
            phase_ = static_cast<int>(best);
            std::fill(eye_.begin(), eye_.end(), 0.0);
            block_samples_ = 0;
        }
    }
}

}  // namespace ed::dsp
