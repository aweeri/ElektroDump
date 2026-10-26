// ElektroDump — SNR estimators (PLAN.md D2, digest §4.4).
// EvmSnrEstimator: decision-directed EVM on post-recovery symbols, rotation-
// invariant per I/Q component (robust to Costas ambiguity/OQPSK staggering).
// M2m4SnrEstimator: 2nd/4th-order moments on raw IQ, no lock needed (fallback
// during acquisition). Both use EMAs, O(1) per sample, no hot-path allocation.
#pragma once

#include <complex>
#include <cstddef>

namespace ed::dsp {

class EvmSnrEstimator {
public:
    // order: 2 = BPSK, 4 = QPSK/OQPSK, 8 = 8PSK.
    explicit EvmSnrEstimator(int order = 2, double alpha = 0.001);

    // Re-arm for a different constellation order (demod switches BPSK/OQPSK).
    void configure(int order, double alpha = 0.001);

    void reset();
    void update(const std::complex<float>* symbols, std::size_t n);

    double signal_power() const { return signal_; }
    double noise_power() const { return noise_; }

    // SNR in dB, clamped to >= 0.
    double snr_db() const;

    std::size_t samples() const { return samples_; }

private:
    int order_;
    double alpha_;
    double beta_;
    double pwr_ = 0.0;
    double pwr_re_ = 0.0;
    double pwr_im_ = 0.0;
    double err_ = 0.0;
    mutable double signal_ = 0.0;
    mutable double noise_ = 0.0;
    std::size_t samples_ = 0;
};

class M2m4SnrEstimator {
public:
    explicit M2m4SnrEstimator(double alpha = 0.001);

    void reset();
    void update(const std::complex<float>* iq, std::size_t n);

    double signal_power() const { return signal_; }
    double noise_power() const { return noise_; }
    double snr_db() const;

    std::size_t samples() const { return samples_; }

private:
    double alpha_;
    double beta_;
    double y1_ = 0.0;
    double y2_ = 0.0;
    mutable double signal_ = 0.0;
    mutable double noise_ = 0.0;
    std::size_t samples_ = 0;
};

// BPSK bit-error-rate estimate from a linear SNR (Eb/N0 approximation).
double bpsk_ber_from_snr_db(double snr_db);

}  // namespace ed::dsp
