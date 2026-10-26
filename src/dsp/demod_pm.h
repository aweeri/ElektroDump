// ElektroDump — GGAK PM demodulator (PLAN.md D2, digest §3).
// GGAK-M is a phase-modulated BPSK subcarrier at ~5 kbaud. Chain: IQ -> boxcar
// decimator -> CarrierPll -> PM-to-BPSK -> RRC -> CostasLoop -> timing -> int8
// soft symbols. SNR: EVM on recovered symbols (primary), M2M4 on raw IQ (fallback).
#pragma once

#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/carrier_pll.h"
#include "dsp/clock_recovery.h"
#include "dsp/costas.h"
#include "dsp/rrc_filter.h"
#include "dsp/snr_estimator.h"

namespace ed::dsp {

struct PmDemodConfig {
    double symbol_rate = 5000.0;
    double rrc_alpha = 0.5;
    double pll_bw = 0.02;
    double costas_bw = 0.02;
    double max_carrier_offset_hz = 50000.0;
    double subcarrier_hz = 0.0;  // 0 => use symbol_rate
    int target_sps = 8;
    int rrc_taps = 33;
};

class PmDemod {
public:
    void configure(const PmDemodConfig& cfg, uint32_t sample_rate);
    void reset();

    // Consumes IQ and appends int8 soft symbols (range -127..127).
    void process(const std::complex<float>* iq, std::size_t n,
                 std::vector<int8_t>& soft_out);

    double snr_db() const;
    bool locked() const;
    double freq_offset_hz() const;
    double symbol_rate() const { return cfg_.symbol_rate; }
    double samples_per_symbol() const { return decim_sps_; }

private:
    PmDemodConfig cfg_;
    uint32_t sample_rate_ = 0;

    CarrierPll pll_;
    RrcFilter rrc_;
    CostasLoop costas_;
    SymbolTimingRecovery clock_;

    EvmSnrEstimator evm_{2, 0.002};
    M2m4SnrEstimator m2m4_{0.001};

    // PM-to-BPSK subcarrier derotation (recursive complex rotation).
    std::complex<float> rot_{1.0f, 0.0f};
    std::complex<float> rot_delta_{1.0f, 0.0f};
    std::size_t rot_count_ = 0;

    std::size_t decim_ = 1;
    double decim_sps_ = 1.0;
    std::complex<float> ma_sum_{0.0f, 0.0f};
    std::size_t ma_count_ = 0;

    double rms_ = 0.0;

    // Scratch buffers (reused across calls; no hot-path allocation).
    std::vector<std::complex<float>> pll_buf_;
    std::vector<std::complex<float>> pm_buf_;
    std::vector<std::complex<float>> decim_buf_;
    std::vector<std::complex<float>> rrc_buf_;
    std::vector<std::complex<float>> costas_buf_;
    std::vector<std::complex<float>> symbols_;

    std::atomic<double> snr_db_{0.0};
    std::atomic<bool> locked_{false};
};

}  // namespace ed::dsp
