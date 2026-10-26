// ElektroDump — convolutional (Viterbi) decoder, rate 1/2, K=7 (PLAN.md D3.4).
// CCSDS code: constraint length 7 (64 states), generators 0o171/0o133; trellis
// and branch metrics re-derived from spec. Streaming: int8 soft symbols in
// (positive favours a 1 bit), hard bits out via sliding-window traceback. Lock
// comes from a decision-directed BER (decode, re-encode, compare).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ed::fec {

class Viterbi {
public:
    Viterbi();

    // g1/g2: generator polynomials. traceback: traceback depth in bits.
    void configure(int g1 = 0171, int g2 = 0133, int traceback = 64);
    void reset();

    // Feed `n` soft symbols; append decoded hard bits (0/1) to `out`.
    void push(const int8_t* soft, std::size_t n, std::vector<uint8_t>& out);

    // Encode `nbits` hard bits to rate 1/2 output bits (test/synthetic helper).
    static void encode(const uint8_t* bits, std::size_t nbits, int g1, int g2,
                       std::vector<uint8_t>& out_bits);

    // Lock thresholds (BER measured after re-encoding).
    void set_lock_threshold(double ber, int outsync_after);

    double ber() const { return ber_; }
    bool locked() const { return locked_; }

private:
    void step(int8_t s0, int8_t s1, std::vector<uint8_t>& out);
    void traceback(std::vector<uint8_t>& out);

    int g1_ = 0171;
    int g2_ = 0133;
    int traceback_ = 64;
    double ber_threshold_ = 0.30;
    int outsync_after_ = 20;

    std::vector<int32_t> metric_;       // 64 current path metrics
    std::vector<int32_t> next_metric_;  // 64 next path metrics
    std::vector<uint8_t> decisions_;    // traceback_ * 64 predecessor states
    std::vector<int8_t> window_soft_;   // 2 * traceback_ soft symbols
    // Precomputed branch tables (2*64), indexed by (state*2 + input bit): next
    // state and the two output bits, avoiding parity recompute on the hot path.
    std::vector<int> next_state_;
    std::vector<uint8_t> out_bits_;
    int steps_in_window_ = 0;
    int since_renorm_ = 0;

    int8_t pending_ = 0;  // half a symbol pair carried between calls
    bool have_pending_ = false;

    int enc_state_ = 0;   // encoder state tracked from emitted bits (for BER)
    double ber_ = 10.0;
    bool locked_ = false;
    int invalid_ = 0;
};

}  // namespace ed::fec
