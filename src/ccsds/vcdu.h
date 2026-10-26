// ElektroDump — CCSDS AOS Virtual Channel Data Unit (PLAN.md D3.8).
// 1024-byte CADU layout: [0..3] ASM 0x1ACFFC1D, [4..9] VCDU primary header
// (version/scid/vcid/counter/replay), [10..11] signalling field (11-bit
// first-header pointer), [12..] M-PDU data field.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ed::ccsds {

struct Vcdu {
    uint8_t version = 0;
    uint16_t spacecraft_id = 0;
    uint8_t vcid = 0;  // 63 == filler
    uint32_t counter = 0;
    bool replay = false;
};

inline constexpr uint8_t kVcidFiller = 63;
inline constexpr std::size_t kVcduHeaderBytes = 6;   // bytes 4..9
inline constexpr std::size_t kMpduDataOffset = 12;   // M-PDU data field start

Vcdu parse_vcdu(const uint8_t* cadu, std::size_t len);

// First-header pointer from the VCDU signalling field (2047 = no packet header).
uint16_t vcdu_first_header_pointer(const uint8_t* cadu, std::size_t len);

}  // namespace ed::ccsds
