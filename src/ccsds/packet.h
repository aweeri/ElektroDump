// ElektroDump — CCSDS Space Packet (PLAN.md D3.8).
#pragma once

#include <cstdint>
#include <vector>

namespace ed::ccsds {

struct CcsdsPacket {
    uint16_t apid = 0;
    uint8_t sequence_flag = 0;  // 0=continuation, 1=start, 2=last, 3=start+last
    uint16_t packet_sequence_count = 0;
    uint16_t packet_length = 0;  // transmitted field (payload size - 1)
    std::vector<uint8_t> payload;
};

}  // namespace ed::ccsds
