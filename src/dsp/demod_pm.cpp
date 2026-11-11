#include "dsp/demod_pm.h"

#include <cstdlib>
#include <cstdio>

#include "core/logging.h"

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

    // BPSK: squaring collapses the modulation, so a line appears at 2x offset.
    {
        double range = cfg_.max_carrier_offset_hz > 0.0
                           ? cfg_.max_carrier_offset_hz
                           : 50000.0;
        range += std::fabs(cfg_.freq_offset_hz);
        coarse_.configure(static_cast<double>(sample_rate_), range, 16384, 2);
    }
    acq_done_ = false;
    acq_offset_hz_ = 0.0;
    acq_buf_.clear();
    pos_lvl_ = 1.0;
    neg_lvl_ = -1.0;
    update_offset_rotator();

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

void PmDemod::update_offset_rotator() {
    // Measured on the raw stream, so it already includes the wideband shift.
    const double total = acq_offset_hz_ != 0.0 ? acq_offset_hz_ : cfg_.freq_offset_hz;
    const double d = -kTwoPi * total / static_cast<double>(sample_rate_);
    off_delta_ = std::complex<float>(static_cast<float>(std::cos(d)),
                                     static_cast<float>(std::sin(d)));
    off_rot_ = std::complex<float>(1.0f, 0.0f);
    off_count_ = 0;
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
    off_rot_ = std::complex<float>(1.0f, 0.0f);
    off_count_ = 0;
    acq_done_ = false;
    acq_offset_hz_ = 0.0;
    acq_buf_.clear();
    coarse_.reset();
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

    if (!acq_done_) {
        acq_buf_.insert(acq_buf_.end(), iq, iq + n);
        const std::size_t need = coarse_.fft_size();
        if (acq_buf_.size() < need) {
            return;
        }
        const double offset = coarse_.estimate(acq_buf_.data(), need);
        const double ratio = coarse_.last_peak_ratio();
        ED_LOG(Info, "ggak") << "coarse acq: offset " << offset
                             << " Hz, peak ratio " << ratio;
        // Apply the estimate only when the spectral line is unambiguous. On a
        // wideband (10 MSPS) capture the squaring estimator aliases onto the
        // front-end passband hump and reports a confident but wrong offset
        // (measured: -610 kHz at ratio 20, -164 kHz at ratio 7.6), so a low
        // ratio must leave the loop alone. A real carrier gives ratios in the
        // hundreds or thousands.
        if (ratio > 50.0 && std::fabs(offset) > 1.0) {
            acq_offset_hz_ = offset;
            update_offset_rotator();
            ED_LOG(Info, "ggak") << "coarse acq: applied " << offset << " Hz";
        }
        acq_done_ = true;
        process_block(acq_buf_.data(), acq_buf_.size(), soft_out);
        acq_buf_.clear();
        acq_buf_.shrink_to_fit();
        return;
    }

    process_block(iq, n, soft_out);
}

void PmDemod::process_block(const std::complex<float>* iq, std::size_t n,
                            std::vector<int8_t>& soft_out) {
    // Debug hook: dump the demapped symbols so they can be compared against an
    // independent reference chain on the same capture.
    static FILE* sym_dump = nullptr;
    static bool sym_dump_checked = false;
    if (!sym_dump_checked) {
        sym_dump_checked = true;
        const char* path = std::getenv("ED_DUMP_SYMS");
        if (path != nullptr) {
            sym_dump = std::fopen(path, "wb");
        }
    }
    if (iq == nullptr || n == 0) {
        return;
    }

    // Boxcar decimation to the target samples/symbol rate; before the PLL so
    // the loop bandwidth stays below the subcarrier.
    decim_buf_.clear();
    decim_buf_.reserve(n / decim_ + 1);
    const bool shift = (cfg_.freq_offset_hz != 0.0) || (acq_offset_hz_ != 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        std::complex<float> sample = iq[i];
        if (shift) {
            sample *= off_rot_;
            off_rot_ *= off_delta_;
            if ((++off_count_ & 0x3FFu) == 0) {
                const float mag = std::abs(off_rot_);
                if (mag > 1e-6f) {
                    off_rot_ /= mag;
                } else {
                    off_rot_ = std::complex<float>(1.0f, 0.0f);
                }
            }
        }
        ma_sum_ += sample;
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

    for (const std::complex<float>& sym : symbols_) {
        const double re = static_cast<double>(sym.real());
        rms_ = 0.99 * rms_ + 0.01 * (re * re);
        // Normalise to unit RMS first, then track the two BPSK levels and slice
        // at their midpoint. Tracking raw levels is unstable here: the rail is
        // ~0.01 in raw units, so a tracker initialised at +-1.0 sits far off the
        // eye for thousands of symbols and biases the bits (measured 12% ones,
        // no ASM) even though the constellation looks clean.
        const double norm = re / (std::sqrt(rms_) + 1e-9);
        if (norm > 0.0) {
            pos_lvl_ += 0.001 * (norm - pos_lvl_);
        } else {
            neg_lvl_ += 0.001 * (norm - neg_lvl_);
        }
        const double mid = 0.5 * (pos_lvl_ + neg_lvl_);
        const double half = 0.5 * (pos_lvl_ - neg_lvl_);
        const double scale = (half > 1e-6 ? 1.0 / half : 1.0) * 100.0;
        soft_out.push_back(to_soft((norm - mid) * scale));
        if (sym_dump != nullptr) {
            std::fputc(soft_out.back() + 128, sym_dump);
        }
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
