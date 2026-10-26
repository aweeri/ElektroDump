#include "dsp/oqpsk_align.h"

#include <cmath>

namespace ed::dsp {

void OqpskAligner::configure(double samples_per_symbol) {
    if (!(samples_per_symbol > 0.0)) {
        samples_per_symbol = 2.0;
    }
    delay_ = 0.5 * samples_per_symbol;
    int_delay_ = static_cast<int>(std::floor(delay_));
    frac_ = delay_ - static_cast<double>(int_delay_);
    reset();
}

void OqpskAligner::reset() {
    const std::size_t size = static_cast<std::size_t>(int_delay_) + 2;
    i_hist_.assign(size, 0.0f);
    pos_ = 0;
}

void OqpskAligner::process(const std::complex<float>* in, std::size_t n,
                           std::complex<float>* out) {
    if (in == nullptr || out == nullptr || i_hist_.empty()) {
        return;
    }
    const std::size_t d = i_hist_.size();
    const std::size_t id = static_cast<std::size_t>(int_delay_);
    for (std::size_t i = 0; i < n; ++i) {
        i_hist_[pos_] = in[i].real();

        // Sample at (i - int_delay_) and the one before it, interpolated by the
        // fractional delay.
        const std::size_t idx_a = (pos_ + d - id) % d;
        const std::size_t idx_b = (pos_ + d - id - 1) % d;
        const float i_delayed = static_cast<float>(
            static_cast<double>(i_hist_[idx_a]) * (1.0 - frac_) +
            static_cast<double>(i_hist_[idx_b]) * frac_);

        out[i] = std::complex<float>(i_delayed, in[i].imag());
        pos_ = (pos_ + 1) % d;
    }
}

}  // namespace ed::dsp
