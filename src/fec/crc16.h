// ElektroDump — CRC-16/CCITT (PLAN.md D3.7, digest §4.3).
// xRIT attaches a 16-bit CCITT CRC to every packet payload: poly 0x1021, init
// 0xFFFF, no reflection, no final XOR ("CRC-16/CCITT-FALSE"). The 256-entry table
// is re-derived from the polynomial at first use.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ed::fec {

// Check value for the ASCII string "123456789" is 0x29B1.
uint16_t crc16_ccitt(const uint8_t* data, std::size_t len,
                     uint16_t init = 0xFFFF);

}  // namespace ed::fec
