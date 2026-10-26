#include "fec/depuncture.h"

namespace ed::fec {

void Depuncture::configure(PunctureRate rate) {
    pattern_.clear();
    index_ = 0;
    switch (rate) {
        case PunctureRate::R1_2:
            enabled_ = false;
            break;
        case PunctureRate::R2_3:
            // 3 punctured symbols per period; expansion to 4 rate-1/2 symbols.
            pattern_ = {1, 2, 1};
            enabled_ = true;
            break;
        case PunctureRate::R3_4:
            pattern_ = {1, 2, 1, 2};
            enabled_ = true;
            break;
        case PunctureRate::R5_6:
            pattern_ = {2, 2, 2, 2, 1, 1};
            enabled_ = true;
            break;
        case PunctureRate::R7_8:
            pattern_ = {2, 2, 2, 2, 2, 2, 1, 1};
            enabled_ = true;
            break;
    }
}

void Depuncture::reset() { index_ = 0; }

void Depuncture::process(const int8_t* in, std::size_t n,
                         std::vector<int8_t>& out) {
    if (!enabled_ || in == nullptr) {
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(in[i]);
        }
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t count = pattern_[index_ % pattern_.size()];
        out.push_back(in[i]);
        for (uint8_t k = 1; k < count; ++k) {
            out.push_back(0);  // erasure
        }
        ++index_;
    }
}

}  // namespace ed::fec
