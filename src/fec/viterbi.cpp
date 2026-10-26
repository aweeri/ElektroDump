#include "fec/viterbi.h"

#include <algorithm>
#include <limits>

namespace ed::fec {

namespace {

constexpr int kStates = 64;      // 1 << (K - 1)
constexpr int kStateMask = 63;
constexpr int32_t kMetricInf = std::numeric_limits<int32_t>::max() / 4;

int parity(int value) {
    value ^= (value >> 16);
    value ^= (value >> 8);
    value ^= (value >> 4);
    value &= 0x0F;
    value ^= (value >> 2);
    value &= 0x03;
    value ^= (value >> 1);
    return value & 1;
}

}  // namespace

Viterbi::Viterbi() {
    metric_.assign(kStates, 0);
    next_metric_.assign(kStates, 0);
    configure();
}

void Viterbi::configure(int g1, int g2, int traceback) {
    g1_ = g1;
    g2_ = g2;
    traceback_ = traceback > 8 ? traceback : 8;
    decisions_.assign(static_cast<std::size_t>(traceback_) * kStates, 0);
    window_soft_.assign(static_cast<std::size_t>(traceback_) * 2, 0);

    next_state_.assign(2 * kStates, 0);
    out_bits_.assign(2 * kStates, 0);
    for (int s = 0; s < kStates; ++s) {
        for (int b = 0; b < 2; ++b) {
            const int idx = s * 2 + b;
            const int r = (b << 6) | s;
            next_state_[static_cast<std::size_t>(idx)] = (r >> 1) & kStateMask;
            out_bits_[static_cast<std::size_t>(idx)] =
                static_cast<uint8_t>(parity(r & g1_) | (parity(r & g2_) << 1));
        }
    }
    reset();
}

void Viterbi::set_lock_threshold(double ber, int outsync_after) {
    ber_threshold_ = ber;
    outsync_after_ = outsync_after > 0 ? outsync_after : 1;
}

void Viterbi::reset() {
    std::fill(metric_.begin(), metric_.end(), 0);
    std::fill(next_metric_.begin(), next_metric_.end(), 0);
    std::fill(decisions_.begin(), decisions_.end(), 0);
    std::fill(window_soft_.begin(), window_soft_.end(), 0);
    steps_in_window_ = 0;
    since_renorm_ = 0;
    pending_ = 0;
    have_pending_ = false;
    enc_state_ = 0;
    ber_ = 10.0;
    locked_ = false;
    invalid_ = 0;
}

void Viterbi::encode(const uint8_t* bits, std::size_t nbits, int g1, int g2,
                     std::vector<uint8_t>& out_bits) {
    int state = 0;
    out_bits.reserve(out_bits.size() + nbits * 2);
    for (std::size_t i = 0; i < nbits; ++i) {
        const int b = bits[i] ? 1 : 0;
        const int r = (b << 6) | state;
        out_bits.push_back(static_cast<uint8_t>(parity(r & g1)));
        out_bits.push_back(static_cast<uint8_t>(parity(r & g2)));
        state = (r >> 1) & kStateMask;
    }
}

void Viterbi::push(const int8_t* soft, std::size_t n,
                   std::vector<uint8_t>& out) {
    if (soft == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!have_pending_) {
            pending_ = soft[i];
            have_pending_ = true;
        } else {
            const int8_t s0 = pending_;
            const int8_t s1 = soft[i];
            have_pending_ = false;
            step(s0, s1, out);
        }
    }
}

void Viterbi::step(int8_t s0, int8_t s1, std::vector<uint8_t>& out) {
    const int32_t inf = kMetricInf;
    std::fill(next_metric_.begin(), next_metric_.end(), inf);
    uint8_t* dec = decisions_.data() +
                   static_cast<std::size_t>(steps_in_window_) * kStates;

    const int v0 = static_cast<int>(s0);
    const int v1 = static_cast<int>(s1);
    const int c0_one = 127 - v0;
    const int c0_zero = 127 + v0;
    const int c1_one = 127 - v1;
    const int c1_zero = 127 + v1;
    for (int idx = 0; idx < 2 * kStates; ++idx) {
        const int s = idx >> 1;
        const int32_t base = metric_[s];
        if (base >= inf) {
            continue;
        }
        const uint8_t ob = out_bits_[static_cast<std::size_t>(idx)];
        const int32_t cost = base + ((ob & 1) ? c0_one : c0_zero) +
                             ((ob & 2) ? c1_one : c1_zero);
        const int sp = next_state_[static_cast<std::size_t>(idx)];
        if (cost < next_metric_[sp]) {
            next_metric_[sp] = cost;
            dec[sp] = static_cast<uint8_t>(s);
        }
    }
    metric_.swap(next_metric_);

    window_soft_[static_cast<std::size_t>(steps_in_window_) * 2] = s0;
    window_soft_[static_cast<std::size_t>(steps_in_window_) * 2 + 1] = s1;
    ++steps_in_window_;

    if (++since_renorm_ >= 256) {
        since_renorm_ = 0;
        const int32_t min = *std::min_element(metric_.begin(), metric_.end());
        if (min != 0) {
            for (int32_t& m : metric_) {
                m -= min;
            }
        }
    }

    if (steps_in_window_ >= traceback_) {
        traceback(out);
        steps_in_window_ = 0;
    }
}

void Viterbi::traceback(std::vector<uint8_t>& out) {
    const int d = traceback_;
    // Best end state.
    int state = 0;
    int32_t best = metric_[0];
    for (int s = 1; s < kStates; ++s) {
        if (metric_[s] < best) {
            best = metric_[s];
            state = s;
        }
    }

    // Walk the survivor path, recording the input bit of each step.
    std::vector<uint8_t> bits(static_cast<std::size_t>(d), 0);
    for (int t = d - 1; t >= 0; --t) {
        bits[static_cast<std::size_t>(t)] =
            static_cast<uint8_t>((state >> 5) & 1);
        const uint8_t pred = decisions_[static_cast<std::size_t>(t) * kStates +
                                        static_cast<std::size_t>(state)];
        state = pred;
    }

    // BER: re-encode the window from the tracked encoder state and compare with
    // the hard-sliced soft symbols.
    int enc = enc_state_;
    int errors = 0;
    for (int t = 0; t < d; ++t) {
        const int b = bits[static_cast<std::size_t>(t)];
        const int r = (b << 6) | enc;
        const int o1 = parity(r & g1_);
        const int o2 = parity(r & g2_);
        enc = (r >> 1) & kStateMask;
        const int8_t h0 = window_soft_[static_cast<std::size_t>(t) * 2] >= 0 ? 1 : 0;
        const int8_t h1 =
            window_soft_[static_cast<std::size_t>(t) * 2 + 1] >= 0 ? 1 : 0;
        if (o1 != h0) {
            ++errors;
        }
        if (o2 != h1) {
            ++errors;
        }
    }
    enc_state_ = enc;

    const double window_ber =
        static_cast<double>(errors) / static_cast<double>(d * 2);
    ber_ = 0.5 * ber_ + 0.5 * window_ber;

    if (window_ber <= ber_threshold_) {
        invalid_ = 0;
        locked_ = true;
    } else if (locked_) {
        if (++invalid_ > outsync_after_) {
            locked_ = false;
            invalid_ = 0;
        }
    }

    out.insert(out.end(), bits.begin(), bits.end());
}

}  // namespace ed::fec
