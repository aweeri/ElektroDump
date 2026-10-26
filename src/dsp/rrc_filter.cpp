#include "dsp/rrc_filter.h"

#include <cmath>

namespace ed::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

// Root-raised-cosine impulse response, re-derived from the standard formula.
double rrc_impulse(double t, double alpha) {
    if (std::fabs(t) < 1e-9) {
        return 1.0 - alpha + 4.0 * alpha / kPi;
    }
    const double four_alpha_t = 4.0 * alpha * t;
    if (std::fabs(std::fabs(four_alpha_t) - 1.0) < 1e-6) {
        // Singularity at |t| = 1 / (4*alpha).
        const double a = (1.0 + 2.0 / kPi) * std::sin(kPi / (4.0 * alpha));
        const double b = (1.0 - 2.0 / kPi) * std::cos(kPi / (4.0 * alpha));
        return (alpha / std::sqrt(2.0)) * (a + b);
    }
    const double num =
        std::sin(kPi * t * (1.0 - alpha)) +
        4.0 * alpha * t * std::cos(kPi * t * (1.0 + alpha));
    const double den = kPi * t * (1.0 - four_alpha_t * four_alpha_t);
    return num / den;
}

void RrcFilter::configure(double sps, double alpha, int taps) {
    if (!(sps > 0.0)) {
        sps = 1.0;
    }
    if (!(alpha >= 0.0) || alpha > 1.0) {
        alpha = 0.5;
    }
    if (taps < 3) {
        taps = 3;
    }
    if ((taps % 2) == 0) {
        ++taps;
    }

    taps_.assign(static_cast<std::size_t>(taps), 0.0f);
    const int mid = taps / 2;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double t = static_cast<double>(i - mid) / sps;
        const double h = rrc_impulse(t, alpha);
        taps_[static_cast<std::size_t>(i)] = static_cast<float>(h);
        sum += h;
    }
    // Normalise DC gain to 1 so the symbol amplitude is predictable.
    if (std::fabs(sum) > 1e-12) {
        for (float& tap : taps_) {
            tap = static_cast<float>(tap / sum);
        }
    }

    history_.assign(taps_.size(), std::complex<float>(0.0f, 0.0f));
    pos_ = 0;
}

void RrcFilter::reset() {
    for (auto& value : history_) {
        value = std::complex<float>(0.0f, 0.0f);
    }
    pos_ = 0;
}

void RrcFilter::process(const std::complex<float>* in, std::size_t n,
                        std::complex<float>* out) {
    if (in == nullptr || out == nullptr || taps_.empty()) {
        return;
    }
    const std::size_t taps = taps_.size();
    const float* tap = taps_.data();
    const std::complex<float>* hist = history_.data();
    for (std::size_t i = 0; i < n; ++i) {
        history_[pos_] = in[i];
        if (++pos_ == taps) {
            pos_ = 0;
        }

        // Convolve over the circular history, oldest (pos_) to newest, wrapping
        // once; the two sub-loops avoid a per-tap modulo on the hot path.
        float acc_re = 0.0f;
        float acc_im = 0.0f;
        std::size_t idx = pos_;
        std::size_t k = 0;
        for (; idx < taps; ++idx, ++k) {
            const std::complex<float> sample = hist[idx];
            const float t = tap[taps - 1 - k];
            acc_re += sample.real() * t;
            acc_im += sample.imag() * t;
        }
        for (idx = 0; k < taps; ++idx, ++k) {
            const std::complex<float> sample = hist[idx];
            const float t = tap[taps - 1 - k];
            acc_re += sample.real() * t;
            acc_im += sample.imag() * t;
        }
        out[i] = std::complex<float>(acc_re, acc_im);
    }
}

}  // namespace ed::dsp
