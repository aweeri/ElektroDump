#include "dsp/demod_pm.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace ed::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

int8_t to_soft(double value) {
    if (value > 127.0) {
        return 127;
    }
    if (value < -127.0) {
        return -127;
    }
    return static_cast<int8_t>(std::lround(value));
}

}  // namespace

void PmDemod::configure(const PmDemodConfig& cfg, uint32_t sample_rate) {
    cfg_ = cfg;
    sample_rate_ = sample_rate;

    if (!(cfg_.symbol_rate > 0.0)) {
        cfg_.symbol_rate = 5000.0;
    }
    if (cfg_.target_sps < 2) {
        cfg_.target_sps = 8;
    }

    // Decimate the 3 MSPS stream down to a manageable samples/symbol rate.
    const double desired_rate =
        static_cast<double>(cfg_.target_sps) * cfg_.symbol_rate;
    decim_ = static_cast<std::size_t>(
        std::max(1.0, std::round(static_cast<double>(sample_rate_) /
                                 desired_rate)));
    decim_sps_ = static_cast<double>(sample_rate_) /
                 (static_cast<double>(decim_) * cfg_.symbol_rate);
    if (decim_sps_ < 2.0) {
        decim_sps_ = 2.0;
    }

    // PLL and derotation run at the decimated rate, keeping the PLL bandwidth
    // below the subcarrier so it tracks the carrier and rejects the subcarrier.
    const double decim_rate =
        static_cast<double>(sample_rate_) / static_cast<double>(decim_);
    pll_.configure(cfg_.pll_bw, cfg_.max_carrier_offset_hz, decim_rate);
    rrc_.configure(decim_sps_, cfg_.rrc_alpha, cfg_.rrc_taps);
    costas_.configure(cfg_.costas_bw, 2);
    clock_.configure(decim_sps_, 64);

    const double subcarrier =
        cfg_.subcarrier_enabled
            ? ((cfg_.subcarrier_hz > 0.0) ? cfg_.subcarrier_hz
                                          : cfg_.symbol_rate)
            : 0.0;
    const double delta = -kTwoPi * subcarrier / decim_rate;
    rot_delta_ = std::complex<float>(static_cast<float>(std::cos(delta)),
                                     static_cast<float>(std::sin(delta)));
    rot_ = std::complex<float>(1.0f, 0.0f);
    rot_count_ = 0;

    ma_sum_ = std::complex<float>(0.0f, 0.0f);
    ma_count_ = 0;
    rms_ = 0.0;

    evm_.reset();
    m2m4_.reset();
    snr_db_.store(0.0, std::memory_order_relaxed);
    locked_.store(false, std::memory_order_relaxed);
}

void PmDemod::reset() {
    pll_.reset();
    rrc_.reset();
    costas_.reset();
    clock_.reset();
    evm_.reset();
    m2m4_.reset();
    rot_ = std::complex<float>(1.0f, 0.0f);
    rot_count_ = 0;
    ma_sum_ = std::complex<float>(0.0f, 0.0f);
    ma_count_ = 0;
    rms_ = 0.0;
    snr_db_.store(0.0, std::memory_order_relaxed);
    locked_.store(false, std::memory_order_relaxed);
}

void PmDemod::process(const std::complex<float>* iq, std::size_t n,
                      std::vector<int8_t>& soft_out) {
    if (iq == nullptr || n == 0) {
        return;
    }

    // Boxcar decimation to the target samples/symbol rate; before the PLL so
    // the loop bandwidth stays below the subcarrier.
    decim_buf_.clear();
    decim_buf_.reserve(n / decim_ + 1);
    for (std::size_t i = 0; i < n; ++i) {
        ma_sum_ += iq[i];
        if (++ma_count_ >= decim_) {
            decim_buf_.push_back(ma_sum_ / static_cast<float>(decim_));
            ma_sum_ = std::complex<float>(0.0f, 0.0f);
            ma_count_ = 0;
        }
    }
    if (decim_buf_.empty()) {
        return;
    }

    pll_buf_.resize(decim_buf_.size());
    pll_.process(decim_buf_.data(), decim_buf_.size(), pll_buf_.data());

    // PM-to-BPSK: keep the phase-modulated rail and derotate the subcarrier.
    pm_buf_.resize(pll_buf_.size());
    for (std::size_t i = 0; i < pll_buf_.size(); ++i) {
        // Put the rail on the real axis: derotation then leaves the BPSK data
        // on I where the Costas loop expects it (an imaginary rail would sit at
        // a degenerate lock point).
        const std::complex<float> rail(pll_buf_[i].imag(), 0.0f);
        pm_buf_[i] = rail * rot_;
        rot_ *= rot_delta_;
        if ((++rot_count_ & 0x3FFu) == 0) {
            const float mag = std::abs(rot_);
            if (mag > 1e-6f) {
                rot_ /= mag;
            } else {
                rot_ = std::complex<float>(1.0f, 0.0f);
            }
        }
    }

    rrc_buf_.resize(pm_buf_.size());
    rrc_.process(pm_buf_.data(), pm_buf_.size(), rrc_buf_.data());

    costas_buf_.resize(rrc_buf_.size());
    costas_.process(rrc_buf_.data(), rrc_buf_.size(), costas_buf_.data());

    symbols_.clear();
    clock_.process(costas_buf_.data(), costas_buf_.size(), symbols_);
    if (symbols_.empty()) {
        return;
    }

    evm_.update(symbols_.data(), symbols_.size());

    {
        constexpr std::size_t kKeep = 8192;
        std::lock_guard<std::mutex> lock(sym_mutex_);
        if (sym_ring_.size() != kKeep) {
            sym_ring_.assign(kKeep, std::complex<float>(0.0f, 0.0f));
            sym_pos_ = 0;
            sym_count_ = 0;
        }
        for (const std::complex<float>& sym : symbols_) {
            sym_ring_[sym_pos_] = sym;
            sym_pos_ = (sym_pos_ + 1) % kKeep;
            if (sym_count_ < kKeep) {
                ++sym_count_;
            }
        }
    }
    m2m4_.update(iq, n);

    for (const std::complex<float>& sym : symbols_) {
        const double re = static_cast<double>(sym.real());
        const double mag2 = re * re;
        rms_ = 0.99 * rms_ + 0.01 * mag2;
        const double scale = 100.0 / std::sqrt(rms_ + 1e-9);
        soft_out.push_back(to_soft(re * scale));
    }

    const double evm_snr = evm_.snr_db();
    const double m2m4_snr = m2m4_.snr_db();
    // Prefer EVM once enough symbols are folded in, else the moment estimator.
    const double snr = (evm_.samples() >= 64) ? evm_snr : m2m4_snr;
    snr_db_.store(snr, std::memory_order_relaxed);
    locked_.store(snr > 3.0 && evm_.samples() >= 64,
                  std::memory_order_relaxed);
}

void PmDemod::constellation(std::vector<std::complex<float>>& out) const {
    std::lock_guard<std::mutex> lock(sym_mutex_);
    out.clear();
    if (sym_count_ == 0) {
        return;
    }
    const std::size_t cap = sym_ring_.size();
    const std::size_t start = (sym_pos_ + cap - sym_count_) % cap;
    out.reserve(sym_count_);
    for (std::size_t i = 0; i < sym_count_; ++i) {
        out.push_back(sym_ring_[(start + i) % cap]);
    }
}

double PmDemod::snr_db() const {
    return snr_db_.load(std::memory_order_relaxed);
}

bool PmDemod::locked() const {
    return locked_.load(std::memory_order_relaxed);
}

double PmDemod::freq_offset_hz() const { return pll_.freq_hz(); }

}  // namespace ed::dsp
