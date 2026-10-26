// ElektroDump — coarse carrier frequency-offset acquisition (PLAN.md D3.3, M5).
// M-th power spectral-line method: raising a PSK signal to the M-th power
// collapses the modulation and leaves a line at M * offset (M = 2 BPSK, 4
// QPSK/OQPSK). The FFT peak gives the offset; parabolic interpolation refines
// it to sub-bin accuracy.
#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

#include "dsp/fft.h"

namespace ed::dsp {

class CoarseAcquisition {
public:
    // order: 2 (BPSK) or 4 (QPSK/OQPSK). fft_size is rounded up to a power of
    // two. max_offset_hz bounds the search (and hence the acquisition range).
    void configure(double sample_rate, double max_offset_hz,
                   std::size_t fft_size, int order);
    void reset();

    // Estimate the carrier offset (Hz) from `n` IQ samples; 0 when too few.
    double estimate(const std::complex<float>* iq, std::size_t n);

    std::size_t fft_size() const { return fft_size_; }
    double max_offset_hz() const { return max_offset_hz_; }

    // Peak-to-mean power ratio of the last estimate: a rough confidence metric.
    double last_peak_ratio() const { return peak_ratio_; }

private:
    double sample_rate_ = 1.0;
    double max_offset_hz_ = 0.0;
    std::size_t fft_size_ = 16384;
    int order_ = 4;
    double peak_ratio_ = 0.0;

    std::unique_ptr<Fft> fft_;
    std::vector<std::complex<float>> powered_;
    std::vector<std::complex<float>> spectrum_;
};

}  // namespace ed::dsp
