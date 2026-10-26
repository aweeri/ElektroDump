#include "dsp/snr_estimator.h"

#include <algorithm>
#include <cmath>

namespace ed::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp_alpha(double alpha) {
    if (!(alpha > 0.0)) {
        return 0.001;
    }
    if (alpha > 1.0) {
        return 1.0;
    }
    return alpha;
}

}  // namespace

EvmSnrEstimator::EvmSnrEstimator(int order, double alpha)
    : order_(order), alpha_(clamp_alpha(alpha)), beta_(1.0 - clamp_alpha(alpha)) {}

void EvmSnrEstimator::configure(int order, double alpha) {
    order_ = order;
    alpha_ = clamp_alpha(alpha);
    beta_ = 1.0 - alpha_;
    reset();
}

void EvmSnrEstimator::reset() {
    pwr_ = 0.0;
    pwr_re_ = 0.0;
    pwr_im_ = 0.0;
    err_ = 0.0;
    signal_ = 0.0;
    noise_ = 0.0;
    samples_ = 0;
}

void EvmSnrEstimator::update(const std::complex<float>* symbols,
                             std::size_t n) {
    if (symbols == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double re = static_cast<double>(symbols[i].real());
        const double im = static_cast<double>(symbols[i].imag());

        const double pwr = re * re + im * im;
        pwr_ = alpha_ * pwr + beta_ * pwr_;
        pwr_re_ = alpha_ * (re * re) + beta_ * pwr_re_;
        pwr_im_ = alpha_ * (im * im) + beta_ * pwr_im_;

        double err_pow = 0.0;
        if (order_ <= 4) {
            // BPSK/QPSK/OQPSK: per-component hard decision; each rail is
            // antipodal, so the reference amplitude is sqrt(E[component^2]).
            const double a_re = std::sqrt(pwr_re_);
            const double a_im = std::sqrt(pwr_im_);
            const double ideal_re = (re >= 0.0) ? a_re : -a_re;
            const double ideal_im = (im >= 0.0) ? a_im : -a_im;
            const double err_re = re - ideal_re;
            const double err_im = im - ideal_im;
            err_pow = err_re * err_re + err_im * err_im;
        } else {
            // 8PSK: nearest constellation point, trying both grid rotations.
            const double a = std::sqrt(pwr_);
            const double step = 2.0 * kPi / static_cast<double>(order_);
            const double angle = std::atan2(im, re);

            const double k0 = std::round(angle / step);
            const double ideal0 = k0 * step;
            const double k1 = std::round((angle - step * 0.5) / step);
            const double ideal1 = k1 * step + step * 0.5;

            const double d0_re = re - a * std::cos(ideal0);
            const double d0_im = im - a * std::sin(ideal0);
            const double d1_re = re - a * std::cos(ideal1);
            const double d1_im = im - a * std::sin(ideal1);

            const double e0 = d0_re * d0_re + d0_im * d0_im;
            const double e1 = d1_re * d1_re + d1_im * d1_im;
            err_pow = (e0 < e1) ? e0 : e1;
        }

        err_ = alpha_ * err_pow + beta_ * err_;
        ++samples_;
    }

    if (!std::isfinite(pwr_)) {
        pwr_ = 0.0;
    }
    if (!std::isfinite(pwr_re_)) {
        pwr_re_ = 0.0;
    }
    if (!std::isfinite(pwr_im_)) {
        pwr_im_ = 0.0;
    }
    if (!std::isfinite(err_)) {
        err_ = 0.0;
    }
}

double EvmSnrEstimator::snr_db() const {
    double ps = pwr_ - err_;
    if (ps < 0.0) {
        ps = 0.0;
    }
    double pn = err_;
    if (pn <= 0.0) {
        pn = 1e-12;
    }
    signal_ = ps;
    noise_ = pn;
    const double ratio = ps / pn;
    if (!(ratio > 0.0) || !std::isfinite(ratio)) {
        return 0.0;
    }
    return std::max(0.0, 10.0 * std::log10(ratio));
}

M2m4SnrEstimator::M2m4SnrEstimator(double alpha)
    : alpha_(clamp_alpha(alpha)), beta_(1.0 - clamp_alpha(alpha)) {}

void M2m4SnrEstimator::reset() {
    y1_ = 0.0;
    y2_ = 0.0;
    signal_ = 0.0;
    noise_ = 0.0;
    samples_ = 0;
}

void M2m4SnrEstimator::update(const std::complex<float>* iq, std::size_t n) {
    if (iq == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double re = static_cast<double>(iq[i].real());
        const double im = static_cast<double>(iq[i].imag());
        const double mag2 = re * re + im * im;
        const double y1 = mag2;
        const double y2 = mag2 * mag2;
        y1_ = alpha_ * y1 + beta_ * y1_;
        y2_ = alpha_ * y2 + beta_ * y2_;
        ++samples_;
    }
    if (!std::isfinite(y1_)) {
        y1_ = 0.0;
    }
    if (!std::isfinite(y2_)) {
        y2_ = 0.0;
    }
}

double M2m4SnrEstimator::snr_db() const {
    const double y1_2 = y1_ * y1_;
    double disc = 2.0 * y1_2 - y2_;
    if (disc < 0.0) {
        disc = 0.0;
    }
    const double root = std::sqrt(disc);
    signal_ = root;
    noise_ = y1_ - root;
    if (noise_ <= 0.0) {
        noise_ = 1e-12;
    }
    const double ratio = signal_ / noise_;
    if (!(ratio > 0.0) || !std::isfinite(ratio)) {
        return 0.0;
    }
    return std::max(0.0, 10.0 * std::log10(ratio));
}

double bpsk_ber_from_snr_db(double snr_db) {
    // BPSK Pb = 0.5 * erfc(sqrt(Eb/N0)); the measured SNR is treated as Eb/N0.
    const double snr_linear = std::pow(10.0, snr_db / 10.0);
    if (!(snr_linear > 0.0) || !std::isfinite(snr_linear)) {
        return 0.5;
    }
    const double ber = 0.5 * std::erfc(std::sqrt(snr_linear));
    if (!std::isfinite(ber)) {
        return 0.5;
    }
    return std::min(0.5, std::max(0.0, ber));
}

}  // namespace ed::dsp
