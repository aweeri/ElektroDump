#include "ccsds/vcdu.h"

namespace ed::ccsds {

Vcdu parse_vcdu(const uint8_t* cadu, std::size_t len) {
    Vcdu v;
    if (cadu == nullptr || len < 12) {
        return v;
    }
    v.version = static_cast<uint8_t>(cadu[4] >> 6);
    v.spacecraft_id = static_cast<uint16_t>(
        ((cadu[4] & 0x3Fu) << 2) | (cadu[5] >> 6));
    v.vcid = static_cast<uint8_t>(cadu[5] & 0x3Fu);
    v.counter = (static_cast<uint32_t>(cadu[6]) << 16) |
                (static_cast<uint32_t>(cadu[7]) << 8) |
                static_cast<uint32_t>(cadu[8]);
    v.replay = (cadu[9] >> 7) != 0;
    return v;
}

uint16_t vcdu_first_header_pointer(const uint8_t* cadu, std::size_t len) {
    if (cadu == nullptr || len < 12) {
        return 2047;
    }
    return static_cast<uint16_t>(((cadu[10] & 0x07u) << 8) | cadu[11]);
}

}  // namespace ed::ccsds
