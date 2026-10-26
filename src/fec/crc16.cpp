#include "fec/crc16.h"

namespace ed::fec {

namespace {

constexpr uint16_t kPoly = 0x1021;

struct Crc16Table {
    uint16_t value[256];
    Crc16Table() {
        for (int i = 0; i < 256; ++i) {
            uint16_t crc = static_cast<uint16_t>(i << 8);
            for (int bit = 0; bit < 8; ++bit) {
                if ((crc & 0x8000u) != 0u) {
                    crc = static_cast<uint16_t>((crc << 1) ^ kPoly);
                } else {
                    crc = static_cast<uint16_t>(crc << 1);
                }
            }
            value[i] = crc;
        }
    }
};

const Crc16Table& table() {
    static const Crc16Table instance;
    return instance;
}

}  // namespace

uint16_t crc16_ccitt(const uint8_t* data, std::size_t len, uint16_t init) {
    const Crc16Table& crc_table = table();
    uint16_t crc = init;
    for (std::size_t i = 0; i < len; ++i) {
        const uint8_t index =
            static_cast<uint8_t>((crc >> 8) ^ static_cast<uint16_t>(data[i]));
        crc = static_cast<uint16_t>((crc << 8) ^ crc_table.value[index]);
    }
    return crc;
}

}  // namespace ed::fec
