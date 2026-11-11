// ElektroDump — PSK demodulator (PLAN.md D3.2/D3.3).
// Chain: IQ -> coarse carrier acq -> RRC -> Costas -> OQPSK align -> Gardner
// -> int8 soft symbols. BPSK (LRIT) emits one soft symbol per symbol; OQPSK
// (HRIT) aligns the rails and emits I then Q, the order the rate-1/2 Viterbi
// decoder consumes.
#pragma once

#include <atomic>
#include <mutex>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dsp/coarse_acq.h"
#include "dsp/costas.h"
#include "dsp/gardner_timing.h"
#include "dsp/oqpsk_align.h"
#include "dsp/rrc_filter.h"
#include "dsp/snr_estimator.h"

namespace ed::dsp {

enum class Constellation { BPSK, QPSK, OQPSK };

struct DemodConfig {
    Constellation constellation = Constellation::BPSK;
    double symbol_rate = 294000.0;
    double rrc_alpha = 0.5;
    double pll_bw = 0.0145;
    int max_sps = 3;
    int rrc_taps = 33;

    // When enabled the demod buffers the first `coarse_acq_fft` samples,
    // estimates the offset and removes it before the Costas loop.
    bool coarse_acq = false;
    double coarse_acq_range_hz = 100000.0;
    int coarse_acq_fft = 16384;
};

class PskDemod {
public:
    void configure(const DemodConfig& cfg, uint32_t sample_rate);
    void reset();

    // Consumes IQ and appends int8 soft symbols (range -127..127).
    void process(const std::complex<float>* iq, std::size_t n,
                 std::vector<int8_t>& soft_out);

    double snr_db() const;
    bool locked() const;
    double symbol_rate() const { return cfg_.symbol_rate; }
    // Most recent symbols for the constellation plot (thread-safe copy).
    void constellation(std::vector<std::complex<float>>& out) const;
    double samples_per_symbol() const { return sps_; }

    // Coarse carrier estimate in Hz (0 until acquisition has run).
    double carrier_offset_hz() const {
        return carrier_offset_hz_.load(std::memory_order_relaxed);
    }
    bool acquired() const { return acquired_; }

private:
    void process_block(const std::complex<float>* iq, std::size_t n,
                       std::vector<int8_t>& soft_out);
    void push_soft(double value, std::vector<int8_t>& soft_out);

    DemodConfig cfg_;
    uint32_t sample_rate_ = 0;
    double sps_ = 1.0;
    bool oqpsk_ = false;

    RrcFilter rrc_;
    OqpskAligner aligner_;
    CostasLoop costas_;
    GardnerTiming clock_;
    EvmSnrEstimator evm_{2, 0.002};
    M2m4SnrEstimator m2m4_{0.001};

    CoarseAcquisition coarse_;
    bool coarse_enabled_ = false;
    bool acquired_ = false;
    double corr_freq_hz_ = 0.0;
    double corr_phase_ = 0.0;
    std::vector<std::complex<float>> acq_buf_;

    double rms_ = 0.0;
    std::vector<std::complex<float>> corr_buf_;
    std::vector<std::complex<float>> rrc_buf_;
    std::vector<std::complex<float>> align_buf_;
    std::vector<std::complex<float>> costas_buf_;
    std::vector<std::complex<float>> symbols_;

    mutable std::mutex sym_mutex_;
    std::vector<std::complex<float>> sym_ring_;
    std::size_t sym_pos_ = 0;
    std::size_t sym_count_ = 0;

    std::atomic<double> snr_db_{0.0};
    std::atomic<double> carrier_offset_hz_{0.0};
    std::atomic<bool> locked_{false};
};

Constellation constellation_from_string(const std::string& name);

}  // namespace ed::dsp
