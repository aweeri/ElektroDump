// ElektroDump — CCSDS pseudo-randomiser (PLAN.md D3.6, digest §4.4).
// Period-255 sequence from the 8-stage LFSR h(x) = x^8 + x^7 + x^4 + x^2 + 1,
// seed 0xFF, output from the MSB; the 255-byte table is re-derived at
// construction. XORed over the CADU body (byte 4 onward) from PN position 0.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ed::fec {

class Derandomizer {
public:
    Derandomizer();

    // XOR the PN sequence into `data`; `offset` is the start within the period.
    void apply(uint8_t* data, std::size_t len, std::size_t offset = 0) const;

    const std::array<uint8_t, 255>& table() const { return table_; }

private:
    std::array<uint8_t, 255> table_{};
};

}  // namespace ed::fec
