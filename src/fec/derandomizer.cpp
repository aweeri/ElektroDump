#include "fec/derandomizer.h"

namespace ed::fec {

namespace {

// Feedback taps of h(x) = x^8 + x^7 + x^4 + x^2 + 1 (excluding the x^8 term).
constexpr uint8_t kFeedbackTaps = 0x95;  // bits 7, 4, 2, 0

uint8_t parity8(uint8_t value) {
    value ^= static_cast<uint8_t>(value >> 4);
    value &= 0x0Fu;
    value ^= static_cast<uint8_t>(value >> 2);
    value &= 0x03u;
    value ^= static_cast<uint8_t>(value >> 1);
    return static_cast<uint8_t>(value & 1u);
}

}  // namespace

Derandomizer::Derandomizer() {
    uint8_t state = 0xFF;
    for (std::size_t i = 0; i < table_.size(); ++i) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; ++bit) {
            const uint8_t out = static_cast<uint8_t>((state >> 7) & 1u);
            const uint8_t feedback = parity8(state & kFeedbackTaps);
            state = static_cast<uint8_t>((state << 1) | feedback);
            byte = static_cast<uint8_t>((byte << 1) | out);
        }
        table_[i] = byte;
    }
}

void Derandomizer::apply(uint8_t* data, std::size_t len,
                         std::size_t offset) const {
    if (data == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < len; ++i) {
        data[i] ^= table_[(offset + i) % table_.size()];
    }
}

}  // namespace ed::fec
