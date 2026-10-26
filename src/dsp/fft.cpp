#include "dsp/fft.h"

#include <cmath>
#include <cstring>

#ifdef ED_HAVE_FFTW3
#include <fftw3.h>
#endif

namespace ed::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Self-contained iterative radix-2 FFT (used when FFTW is unavailable).
void fft_radix2(std::complex<float>* a, std::size_t n) {
    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<float> wlen(static_cast<float>(std::cos(ang)),
                                       static_cast<float>(std::sin(ang)));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

}  // namespace

Fft::Fft(std::size_t size) : size_(size) {
    scratch_.assign(size_, std::complex<float>(0.0f, 0.0f));
#ifdef ED_HAVE_FFTW3
    // FFTW_ESTIMATE plans can be executed on other same-size arrays (new-array execute).
    plan_ = fftwf_plan_dft_1d(static_cast<int>(size_),
                              reinterpret_cast<fftwf_complex*>(scratch_.data()),
                              reinterpret_cast<fftwf_complex*>(scratch_.data()),
                              FFTW_FORWARD, FFTW_ESTIMATE);
#endif
}

Fft::~Fft() {
#ifdef ED_HAVE_FFTW3
    if (plan_ != nullptr) {
        fftwf_destroy_plan(static_cast<fftwf_plan>(plan_));
    }
#endif
}

void Fft::forward(const std::complex<float>* in, std::complex<float>* out) {
#ifdef ED_HAVE_FFTW3
    if (plan_ != nullptr) {
        fftwf_execute_dft(static_cast<fftwf_plan>(plan_),
                          reinterpret_cast<fftwf_complex*>(
                              const_cast<std::complex<float>*>(in)),
                          reinterpret_cast<fftwf_complex*>(out));
        return;
    }
#endif
    std::memcpy(out, in, size_ * sizeof(std::complex<float>));
    fft_radix2(out, size_);
}

void Fft::power_db(const std::complex<float>* in, float* out_db) {
    forward(in, scratch_.data());
    const float norm = 1.0f / static_cast<float>(size_ * size_);
    const std::size_t half = size_ / 2;
    for (std::size_t i = 0; i < size_; ++i) {
        // fft-shift: DC in the middle, negative frequencies first.
        const std::size_t src = (i + half) % size_;
        const float re = scratch_[src].real();
        const float im = scratch_[src].imag();
        const float p = (re * re + im * im) * norm;
        out_db[i] = 10.0f * std::log10(p + 1e-12f);
    }
}

}  // namespace ed::dsp
