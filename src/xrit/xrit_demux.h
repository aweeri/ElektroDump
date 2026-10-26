// ElektroDump — xRIT demultiplexer (PLAN.md D3.9, digest §4.3).
// CADU -> per-VCID AOS demux -> per-APID file assembly -> CRC-16 recovery. One
// XritFile per (vcid, apid); completed files are handed back to the caller.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "ccsds/aos_demux.h"
#include "xrit/xrit_file.h"

namespace ed::xrit {

struct DemuxStats {
    uint64_t cadus = 0;
    uint64_t vcdu_filler = 0;
    uint64_t files = 0;
    uint64_t files_recovered = 0;
    uint64_t packets_dropped = 0;
};

class XritDemux {
public:
    explicit XritDemux(std::size_t mpdu_size = 884, bool check_crc = true);

    void reset();

    // Feed one CADU; append files finalized by this CADU to `out`.
    void push_cadu(const uint8_t* cadu, std::size_t len,
                   std::vector<XritFile>& out);

    const DemuxStats& stats() const { return stats_; }

private:
    void finalize(XritFile& file, std::vector<XritFile>& out);

    std::size_t mpdu_size_ = 884;
    bool check_crc_ = true;
    std::map<int, ccsds::AosDemuxer> demuxers_;  // per VCID
    std::map<int, std::map<int, XritFile>> wip_; // vcid -> apid -> file
    std::vector<ccsds::CcsdsPacket> packets_;
    DemuxStats stats_;
};

}  // namespace ed::xrit
