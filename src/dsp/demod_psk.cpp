#include "dsp/demod_psk.h"

#include <cmath>

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

Constellation constellation_from_string(const std::string& name) {
    if (name == "qpsk") {
        return Constellation::QPSK;
    }
    if (name == "oqpsk") {
        return Constellation::OQPSK;
    }
    return Constellation::BPSK;
}

void PskDemod::configure(const DemodConfig& cfg, uint32_t sample_rate) {
    cfg_ = cfg;
    sample_rate_ = sample_rate;
    if (!(cfg_.symbol_rate > 0.0)) {
        cfg_.symbol_rate = 294000.0;
    }
    if (cfg_.rrc_taps < 3) {
        cfg_.rrc_taps = 33;
    }

    sps_ = static_cast<double>(sample_rate_) / cfg_.symbol_rate;
    if (!(sps_ > 1.0)) {
        sps_ = 2.0;
    }

    oqpsk_ = (cfg_.constellation == Constellation::OQPSK);
    const int order = (cfg_.constellation == Constellation::BPSK) ? 2 : 4;

    rrc_.configure(sps_, cfg_.rrc_alpha, cfg_.rrc_taps);
    if (oqpsk_) {
        aligner_.configure(sps_);
    }
    costas_.configure(cfg_.pll_bw, order);
    evm_.configure(order, 0.002);
    // OQPSK symbol instants sit half a symbol later (I rail delayed by T/2).
    const double initial_delay =
        static_cast<double>(rrc_.delay()) + (oqpsk_ ? 0.5 * sps_ : 0.0);
    clock_.configure(sps_, 0.05, 0.0005, initial_delay);

    coarse_enabled_ = cfg_.coarse_acq;
    if (coarse_enabled_) {
        coarse_.configure(static_cast<double>(sample_rate_),
                          cfg_.coarse_acq_range_hz,
                          static_cast<std::size_t>(cfg_.coarse_acq_fft), order);
    }
    acquired_ = false;
    corr_freq_hz_ = 0.0;
    corr_phase_ = 0.0;
    acq_buf_.clear();

    rms_ = 0.0;
    m2m4_.reset();
    snr_db_.store(0.0, std::memory_order_relaxed);
    carrier_offset_hz_.store(0.0, std::memory_order_relaxed);
    locked_.store(false, std::memory_order_relaxed);
}

void PskDemod::reset() {
    rrc_.reset();
    if (oqpsk_) {
        aligner_.reset();
    }
    costas_.reset();
    clock_.reset();
    coarse_.reset();
    acquired_ = false;
    corr_freq_hz_ = 0.0;
    corr_phase_ = 0.0;
    acq_buf_.clear();
    rms_ = 0.0;
    evm_.reset();
    m2m4_.reset();
    snr_db_.store(0.0, std::memory_order_relaxed);
    carrier_offset_hz_.store(0.0, std::memory_order_relaxed);
    locked_.store(false, std::memory_order_relaxed);
}

void PskDemod::push_soft(double value, std::vector<int8_t>& soft_out) {
    const double scale = 100.0 / std::sqrt(rms_ + 1e-9);
    soft_out.push_back(to_soft(value * scale));
}

void PskDemod::process(const std::complex<float>* iq, std::size_t n,
                       std::vector<int8_t>& soft_out) {
    if (iq == nullptr || n == 0) {
        return;
    }

    // Buffer a full FFT block, estimate the carrier offset once, then process
    // the buffered samples with the correction applied.
    if (coarse_enabled_ && !acquired_) {
        acq_buf_.insert(acq_buf_.end(), iq, iq + n);
        const std::size_t need = static_cast<std::size_t>(cfg_.coarse_acq_fft);
        if (acq_buf_.size() < need) {
            return;
        }
        corr_freq_hz_ = coarse_.estimate(acq_buf_.data(), acq_buf_.size());
        carrier_offset_hz_.store(corr_freq_hz_, std::memory_order_relaxed);
        acquired_ = true;
        process_block(acq_buf_.data(), acq_buf_.size(), soft_out);
        acq_buf_.clear();
        acq_buf_.shrink_to_fit();
        return;
    }

    process_block(iq, n, soft_out);
}

void PskDemod::process_block(const std::complex<float>* iq, std::size_t n,
                             std::vector<int8_t>& soft_out) {
    const std::complex<float>* src = iq;
    if (coarse_enabled_ && corr_freq_hz_ != 0.0) {
        corr_buf_.resize(n);
        // Rotation is exp(-j*corr_phase_); a positive offset advances phase.
        const double dphi =
            kTwoPi * corr_freq_hz_ / static_cast<double>(sample_rate_);
        for (std::size_t i = 0; i < n; ++i) {
            const double c = std::cos(corr_phase_);
            const double s = std::sin(corr_phase_);
            const double re = static_cast<double>(iq[i].real());
            const double im = static_cast<double>(iq[i].imag());
            corr_buf_[i] = std::complex<float>(
                static_cast<float>(re * c + im * s),
                static_cast<float>(im * c - re * s));
            corr_phase_ += dphi;
            if (corr_phase_ > kPi) {
                corr_phase_ -= kTwoPi;
            } else if (corr_phase_ < -kPi) {
                corr_phase_ += kTwoPi;
            }
        }
        src = corr_buf_.data();
    }

    rrc_buf_.resize(n);
    rrc_.process(src, n, rrc_buf_.data());

    // Carrier recovery must precede OQPSK alignment: the aligner delays only
    // the real part by T/2, valid only once derotated. A residual carrier makes
    // the rails cross-couple and inverts the Gardner timing S-curve.
    costas_buf_.resize(n);
    costas_.process(rrc_buf_.data(), n, costas_buf_.data());

    // OQPSK: align the I rail with the Q rail (turns it into QPSK).
    const std::complex<float>* aligned = costas_buf_.data();
    if (oqpsk_) {
        align_buf_.resize(n);
        aligner_.process(costas_buf_.data(), n, align_buf_.data());
        aligned = align_buf_.data();
    }

    symbols_.clear();
    clock_.process(aligned, n, symbols_);
    if (symbols_.empty()) {
        return;
    }

    evm_.update(symbols_.data(), symbols_.size());
    m2m4_.update(src, n);

    // BPSK emits the I rail; OQPSK emits I then Q, the order Viterbi consumes.
    for (const std::complex<float>& sym : symbols_) {
        const double re = static_cast<double>(sym.real());
        const double im = static_cast<double>(sym.imag());
        const double power = re * re + (oqpsk_ ? im * im : 0.0);
        rms_ = 0.99 * rms_ + 0.01 * power;
        push_soft(re, soft_out);
        if (oqpsk_) {
            push_soft(im, soft_out);
        }
    }

    const double evm_snr = evm_.snr_db();
    const double m2m4_snr = m2m4_.snr_db();
    const double snr = (evm_.samples() >= 64) ? evm_snr : m2m4_snr;
    snr_db_.store(snr, std::memory_order_relaxed);
    locked_.store(snr > 3.0 && evm_.samples() >= 64,
                  std::memory_order_relaxed);
}

double PskDemod::snr_db() const {
    return snr_db_.load(std::memory_order_relaxed);
}

bool PskDemod::locked() const {
    return locked_.load(std::memory_order_relaxed);
}

}  // namespace ed::dsp
