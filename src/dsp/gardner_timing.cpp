#include "dsp/gardner_timing.h"

#include <algorithm>

namespace ed::dsp {

void GardnerTiming::configure(double samples_per_symbol, double phase_gain,
                              double period_gain, double initial_delay) {
    nominal_ = samples_per_symbol > 1.0 ? samples_per_symbol : 2.0;
    period_ = nominal_;
    phase_gain_ = phase_gain;
    period_gain_ = period_gain;
    initial_delay_ = initial_delay;
    reset();
}

void GardnerTiming::reset() {
    period_ = nominal_;
    next_sym_ = initial_delay_;
    next_mid_ = initial_delay_ - 0.5 * period_;
    index_ = 0;
    s0_ = std::complex<float>(0.0f, 0.0f);
    s1_ = std::complex<float>(0.0f, 0.0f);
    prev_sym_ = std::complex<float>(0.0f, 0.0f);
    mid_ = std::complex<float>(0.0f, 0.0f);
    have_prev_ = false;
    have_mid_ = false;
}

std::complex<float> GardnerTiming::interpolate(double t) const {
    // s1_ is the sample at (index_ - 2), s0_ at (index_ - 1); interpolate
    // between them for t in [index_ - 2, index_ - 1].
    const double frac = t - (static_cast<double>(index_) - 2.0);
    const double clamped = frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac);
    return s1_ + (s0_ - s1_) * static_cast<float>(clamped);
}

void GardnerTiming::process(const std::complex<float>* in, std::size_t n,
                            std::vector<std::complex<float>>& out) {
    if (in == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        s1_ = s0_;
        s0_ = in[i];
        ++index_;
        if (index_ < 2) {
            continue;
        }
        const double t = static_cast<double>(index_ - 1);  // time of s0_

        if (next_mid_ > t - 1.0 && next_mid_ <= t) {
            mid_ = interpolate(next_mid_);
            have_mid_ = true;
            next_mid_ += period_;
        }

        if (next_sym_ > t - 1.0 && next_sym_ <= t) {
            const std::complex<float> sym = interpolate(next_sym_);
            if (have_prev_ && have_mid_) {
                const double e =
                    (static_cast<double>(sym.real()) -
                     static_cast<double>(prev_sym_.real())) *
                        static_cast<double>(mid_.real()) +
                    (static_cast<double>(sym.imag()) -
                     static_cast<double>(prev_sym_.imag())) *
                        static_cast<double>(mid_.imag());
                next_sym_ -= phase_gain_ * e;
                next_mid_ -= phase_gain_ * e;
                period_ -= period_gain_ * e;
                period_ = std::clamp(period_, nominal_ * 0.9, nominal_ * 1.1);
            }
            prev_sym_ = sym;
            have_prev_ = true;
            out.push_back(sym);
            next_sym_ += period_;
        }
    }
}

}  // namespace ed::dsp
