// ElektroDump — AOS demultiplexer: VCDU -> CCSDS packets (PLAN.md D3.8).
// One instance per VCID. Reassembles CCSDS Space Packets from the M-PDU data
// field, honouring the first-header pointer and packets that continue across CADUs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ccsds/packet.h"

namespace ed::ccsds {

class AosDemuxer {
public:
    explicit AosDemuxer(std::size_t mpdu_data_size = 884)
        : mpdu_size_(mpdu_data_size) {}

    void reset();

    // Feed one 1024-byte CADU; append every completed packet to `out`.
    void push(const uint8_t* cadu, std::size_t len, std::vector<CcsdsPacket>& out);

private:
    void feed_continuation(const uint8_t* data, std::size_t n,
                           std::vector<CcsdsPacket>& out);
    void emit(std::vector<CcsdsPacket>& out);

    std::size_t mpdu_size_ = 884;
    bool active_ = false;
    CcsdsPacket current_;
    std::size_t remaining_ = 0;
    std::vector<uint8_t> header_carry_;
};

}  // namespace ed::ccsds
