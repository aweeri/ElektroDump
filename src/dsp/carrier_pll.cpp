#include "dsp/carrier_pll.h"

#include <cmath>

namespace ed::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double wrap_phase(double phase) {
    while (phase > kPi) {
        phase -= kTwoPi;
    }
    while (phase < -kPi) {
        phase += kTwoPi;
    }
    return phase;
}

}  // namespace

void CarrierPll::configure(double loop_bw, double max_offset_hz,
                           double sample_rate) {
    if (!(loop_bw > 0.0)) {
        loop_bw = 0.02;
    }
    if (!(sample_rate > 0.0)) {
        sample_rate = 1.0;
    }
    sample_rate_ = sample_rate;

    const double damping = std::sqrt(2.0) / 2.0;
    const double denom = 1.0 + 2.0 * damping * loop_bw + loop_bw * loop_bw;
    alpha_ = (4.0 * damping * loop_bw) / denom;
    beta_ = (4.0 * loop_bw * loop_bw) / denom;

    max_freq_ = kTwoPi * std::fabs(max_offset_hz) / sample_rate_;
    reset();
}

void CarrierPll::reset() {
    phase_ = 0.0;
    freq_ = 0.0;
}

void CarrierPll::process(const std::complex<float>* in, std::size_t n,
                         std::complex<float>* out) {
    if (in == nullptr || out == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::complex<float> sample = in[i];

        // Mix the input with the VCO.
        const double c = std::cos(phase_);
        const double s = std::sin(phase_);
        const std::complex<float> mixed(
            static_cast<float>(sample.real() * c + sample.imag() * s),
            static_cast<float>(sample.imag() * c - sample.real() * s));
        out[i] = mixed;

        // Phase detector: atan2(Q, I) - phase.
        double error = std::atan2(static_cast<double>(sample.imag()),
                                  static_cast<double>(sample.real())) -
                       phase_;
        error = wrap_phase(error);

        freq_ += beta_ * error;
        if (freq_ > max_freq_) {
            freq_ = max_freq_;
        } else if (freq_ < -max_freq_) {
            freq_ = -max_freq_;
        }
        phase_ = wrap_phase(phase_ + freq_ + alpha_ * error);

        if (!std::isfinite(phase_) || !std::isfinite(freq_)) {
            phase_ = 0.0;
            freq_ = 0.0;
        }
    }
}

double CarrierPll::freq_hz() const {
    return freq_ * sample_rate_ / kTwoPi;
}

}  // namespace ed::dsp
