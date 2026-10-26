#include "dsp/coarse_acq.h"

#include <cmath>

namespace ed::dsp {

void CoarseAcquisition::configure(double sample_rate, double max_offset_hz,
                                  std::size_t fft_size, int order) {
    sample_rate_ = sample_rate > 0.0 ? sample_rate : 1.0;
    max_offset_hz_ = max_offset_hz > 0.0 ? max_offset_hz : 0.0;
    order_ = (order == 2) ? 2 : 4;

    // Round the FFT size up to a power of two (the FFT kernel requires it).
    std::size_t n = 256;
    while (n < fft_size) {
        n <<= 1;
    }
    fft_size_ = n;

    fft_ = std::make_unique<Fft>(fft_size_);
    powered_.assign(fft_size_, std::complex<float>(0.0f, 0.0f));
    spectrum_.assign(fft_size_, std::complex<float>(0.0f, 0.0f));
    peak_ratio_ = 0.0;
}

void CoarseAcquisition::reset() {
    peak_ratio_ = 0.0;
}

double CoarseAcquisition::estimate(const std::complex<float>* iq,
                                   std::size_t n) {
    if (iq == nullptr || fft_ == nullptr || n < fft_size_) {
        return 0.0;
    }

    // Use the most recent fft_size_ samples.
    const std::complex<float>* src = iq + (n - fft_size_);
    for (std::size_t i = 0; i < fft_size_; ++i) {
        const std::complex<float> s = src[i];
        std::complex<float> y = s;
        for (int p = 1; p < order_; ++p) {
            y *= s;
        }
        powered_[i] = y;
    }

    fft_->forward(powered_.data(), spectrum_.data());

    const double bin_hz = sample_rate_ / static_cast<double>(fft_size_);
    const int size = static_cast<int>(fft_size_);
    const int half = size / 2;

    // The line sits at order_ * offset, so the bin search window is
    // order_ * max_offset / bin_hz.
    int max_bin = static_cast<int>(max_offset_hz_ * static_cast<double>(order_) /
                                   bin_hz);
    if (max_bin > half - 1) {
        max_bin = half - 1;
    }
    if (max_bin < 1) {
        max_bin = 1;
    }

    int best = 0;
    double best_mag = -1.0;
    double sum_mag = 0.0;
    int count = 0;
    for (int k = -max_bin; k <= max_bin; ++k) {
        const int idx = (k + size) % size;
        const double mag = std::norm(spectrum_[static_cast<std::size_t>(idx)]);
        sum_mag += mag;
        ++count;
        if (mag > best_mag) {
            best_mag = mag;
            best = k;
        }
    }

    // Parabolic interpolation around the peak for sub-bin accuracy.
    double delta = 0.0;
    if (best > -max_bin && best < max_bin) {
        const int i0 = (best - 1 + size) % size;
        const int i1 = (best + size) % size;
        const int i2 = (best + 1 + size) % size;
        const double m0 = std::norm(spectrum_[static_cast<std::size_t>(i0)]);
        const double m1 = std::norm(spectrum_[static_cast<std::size_t>(i1)]);
        const double m2 = std::norm(spectrum_[static_cast<std::size_t>(i2)]);
        const double denom = m0 - 2.0 * m1 + m2;
        if (std::fabs(denom) > 1e-12) {
            delta = 0.5 * (m0 - m2) / denom;
            if (delta > 0.5) {
                delta = 0.5;
            } else if (delta < -0.5) {
                delta = -0.5;
            }
        }
    }

    const double mean = count > 0 ? sum_mag / static_cast<double>(count) : 1.0;
    peak_ratio_ = mean > 0.0 ? best_mag / mean : 0.0;

    const double bin = static_cast<double>(best) + delta;
    return bin * bin_hz / static_cast<double>(order_);
}

}  // namespace ed::dsp
