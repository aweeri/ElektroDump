// ElektroDump — depuncturing for the CCSDS convolutional code (PLAN.md D3.4).
// Elektro-L LRIT/HRIT use the fixed rate 1/2 code, so this is a no-op there; the
// other rates are provided for completeness. Expands a punctured soft stream back
// to rate 1/2 by inserting erasure symbols where the transmitter removed them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ed::fec {

enum class PunctureRate { R1_2, R2_3, R3_4, R5_6, R7_8 };

class Depuncture {
public:
    void configure(PunctureRate rate);
    void reset();

    // Expands `n` soft symbols, appending the rate-1/2 stream to `out`.
    void process(const int8_t* in, std::size_t n, std::vector<int8_t>& out);

    bool enabled() const { return enabled_; }

private:
    bool enabled_ = false;
    // Outputs per input symbol, cycled; erasure (0) inserted when a slot is >= 2.
    std::vector<uint8_t> pattern_;
    std::size_t index_ = 0;
};

}  // namespace ed::fec
