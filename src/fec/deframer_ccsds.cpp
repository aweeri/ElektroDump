#include "fec/deframer_ccsds.h"

namespace ed::fec {

namespace {

constexpr int kSyncingGoodAsm = 10;  // consecutive good ASMs to declare lock
constexpr int kSyncingBadAsm = 3;    // consecutive bad ASMs to lose lock

}  // namespace

void CcsdsDeframer::configure(std::size_t cadu_bits, uint32_t asm_word) {
    cadu_bits_ = cadu_bits >= 64 ? cadu_bits : 64;
    cadu_bytes_ = (cadu_bits_ + 7) / 8;
    asm_ = asm_word;
    asm_inv_ = asm_word ^ 0xFFFFFFFFu;
    frame_.assign(cadu_bytes_, 0);
    reset();
}

void CcsdsDeframer::reset() {
    state_ = State::NoSync;
    inversion_ = false;
    shifter_ = 0;
    collecting_ = false;
    bit_of_frame_ = 0;
    expecting_asm_ = false;
    asm_acc_ = 0;
    asm_count_ = 0;
    good_asm_ = 0;
    bad_asm_ = 0;
    frames_ = 0;
}

int CcsdsDeframer::bit_errors(uint32_t a, uint32_t b) {
    uint32_t x = a ^ b;
    int count = 0;
    while (x != 0u) {
        x &= (x - 1u);
        ++count;
    }
    return count;
}

void CcsdsDeframer::begin_frame() {
    frame_.assign(cadu_bytes_, 0);
    frame_[0] = static_cast<uint8_t>((asm_ >> 24) & 0xFFu);
    frame_[1] = static_cast<uint8_t>((asm_ >> 16) & 0xFFu);
    frame_[2] = static_cast<uint8_t>((asm_ >> 8) & 0xFFu);
    frame_[3] = static_cast<uint8_t>(asm_ & 0xFFu);
    bit_of_frame_ = 32;
    collecting_ = true;
    expecting_asm_ = false;
}

void CcsdsDeframer::write_body_bit(uint8_t bit) {
    if ((bit ^ static_cast<uint8_t>(inversion_)) != 0u) {
        const std::size_t byte = bit_of_frame_ / 8;
        const int shift = 7 - static_cast<int>(bit_of_frame_ % 8);
        frame_[byte] |= static_cast<uint8_t>(1u << shift);
    }
    ++bit_of_frame_;
}

void CcsdsDeframer::finish_frame(std::vector<std::vector<uint8_t>>& out) {
    out.push_back(frame_);
    ++frames_;
    collecting_ = false;
    expecting_asm_ = true;
    asm_acc_ = 0;
    asm_count_ = 0;
}

void CcsdsDeframer::on_next_asm(bool good,
                                std::vector<std::vector<uint8_t>>& out) {
    (void)out;
    if (good) {
        ++good_asm_;
        bad_asm_ = 0;
        if (state_ == State::Syncing && good_asm_ > kSyncingGoodAsm) {
            state_ = State::Synced;
        }
        begin_frame();
    } else {
        good_asm_ = 0;
        ++bad_asm_;
        expecting_asm_ = false;
        collecting_ = false;
        if (state_ == State::Synced) {
            state_ = State::NoSync;
        } else if (state_ == State::Syncing && bad_asm_ >= kSyncingBadAsm) {
            state_ = State::NoSync;
        }
    }
}

void CcsdsDeframer::push(const uint8_t* bits, std::size_t n,
                         std::vector<std::vector<uint8_t>>& out) {
    if (bits == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t bit = bits[i] ? 1u : 0u;

        if (collecting_) {
            write_body_bit(bit);
            if (bit_of_frame_ >= cadu_bits_) {
                finish_frame(out);
            }
            continue;
        }

        if (expecting_asm_) {
            asm_acc_ = (asm_acc_ << 1) | bit;
            if (++asm_count_ >= 32) {
                const uint32_t target = inversion_ ? asm_inv_ : asm_;
                const bool good =
                    bit_errors(asm_acc_, target) <= max_errors_;
                on_next_asm(good, out);
            }
            continue;
        }

        // NoSync: slide the shifter and look for the ASM in either polarity.
        shifter_ = (shifter_ << 1) | bit;
        if (bit_errors(shifter_, asm_) <= max_errors_) {
            inversion_ = false;
            good_asm_ = 1;
            bad_asm_ = 0;
            state_ = State::Syncing;
            begin_frame();
        } else if (bit_errors(shifter_, asm_inv_) <= max_errors_) {
            inversion_ = true;
            good_asm_ = 1;
            bad_asm_ = 0;
            state_ = State::Syncing;
            begin_frame();
        }
    }
}

}  // namespace ed::fec
