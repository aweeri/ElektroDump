// ElektroDump — complex FFT for the waterfall (PLAN.md D2).
// Uses FFTW3f when available, else a self-contained radix-2 Cooley-Tukey kernel.
// Only power-of-two sizes are supported (the waterfall always uses 1024).
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ed::dsp {

class Fft {
public:
    explicit Fft(std::size_t size);
    ~Fft();

    Fft(const Fft&) = delete;
    Fft& operator=(const Fft&) = delete;

    std::size_t size() const { return size_; }

    // Forward transform of `in` (size() samples) into `out` (size() samples).
    void forward(const std::complex<float>* in, std::complex<float>* out);

    // Convenience: power spectrum in dB (fft-shifted, DC in the middle).
    void power_db(const std::complex<float>* in, float* out_db);

private:
    std::size_t size_ = 0;
    std::vector<std::complex<float>> scratch_;
#ifdef ED_HAVE_FFTW3
    void* plan_ = nullptr;  // fftwf_plan
#endif
};

}  // namespace ed::dsp
