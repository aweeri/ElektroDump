#include "xrit/xrit_demux.h"

#include "ccsds/vcdu.h"
#include "fec/crc16.h"

namespace ed::xrit {

namespace {

constexpr uint16_t kIdleApid = 2047;
constexpr std::size_t kFirstPayloadSkip = 10;  // first packet in-payload header

}  // namespace

XritDemux::XritDemux(std::size_t mpdu_size, bool check_crc)
    : mpdu_size_(mpdu_size), check_crc_(check_crc) {}

void XritDemux::reset() {
    demuxers_.clear();
    wip_.clear();
    stats_ = DemuxStats{};
}

void XritDemux::finalize(XritFile& file, std::vector<XritFile>& out) {
    ++stats_.files;
    if (file.crc_recovered) {
        ++stats_.files_recovered;
    }
    out.push_back(file);
    file.reset();
}

void XritDemux::push_cadu(const uint8_t* cadu, std::size_t len,
                          std::vector<XritFile>& out) {
    if (cadu == nullptr) {
        return;
    }
    ++stats_.cadus;
    const ccsds::Vcdu vcdu = ccsds::parse_vcdu(cadu, len);
    if (vcdu.vcid == ccsds::kVcidFiller) {
        ++stats_.vcdu_filler;
        return;
    }

    const int vcid = vcdu.vcid;
    auto demux_it = demuxers_.find(vcid);
    if (demux_it == demuxers_.end()) {
        demux_it = demuxers_.emplace(vcid, ccsds::AosDemuxer(mpdu_size_)).first;
        wip_[vcid] = {};
    }

    packets_.clear();
    demux_it->second.push(cadu, len, packets_);

    for (ccsds::CcsdsPacket& packet : packets_) {
        if (packet.apid == kIdleApid || packet.payload.size() < 2) {
            continue;
        }
        std::map<int, XritFile>& files = wip_[vcid];
        XritFile& file = files[packet.apid];

        const std::size_t n = packet.payload.size();
        const uint16_t crc =
            static_cast<uint16_t>((static_cast<uint16_t>(packet.payload[n - 2])
                                   << 8) |
                                  packet.payload[n - 1]);
        const bool crc_ok =
            !check_crc_ ||
            crc == ed::fec::crc16_ccitt(packet.payload.data(), n - 2);

        if (!crc_ok) {
            // Recover a middle segment when the headers are already known;
            // otherwise drop the file.
            bool can_continue = false;
            if (file.file_in_progress && packet.sequence_flag == 0 &&
                file.header_parsed && file.has_header(PrimaryHeader::kType)) {
                const PrimaryHeader primary = file.primary();
                can_continue =
                    primary.file_type_code == 0 &&
                    file.has_header(ImageStructureRecord::kType);
            }
            if (can_continue) {
                file.crc_recovered = true;
                file.data.insert(file.data.end(), packet.payload.begin(),
                                 packet.payload.end() - 2);
                continue;
            }
            ++stats_.packets_dropped;
            file.file_in_progress = false;
            file.data.clear();
            continue;
        }

        if (packet.sequence_flag == 1 || packet.sequence_flag == 3) {
            if (file.file_in_progress) {
                finalize(file, out);
            }
            file.data.clear();
            file.crc_recovered = false;
            file.data.insert(file.data.end(),
                             packet.payload.begin() + kFirstPayloadSkip,
                             packet.payload.end() - 2);
            file.vcid = vcid;
            file.apid = packet.apid;
            file.header_parsed = false;
            file.file_in_progress = true;
        } else if (packet.sequence_flag == 0) {
            if (file.file_in_progress) {
                file.data.insert(file.data.end(), packet.payload.begin(),
                                 packet.payload.end() - 2);
            }
        } else if (packet.sequence_flag == 2) {
            if (file.file_in_progress) {
                file.data.insert(file.data.end(), packet.payload.begin(),
                                 packet.payload.end() - 2);
                finalize(file, out);
                continue;
            }
        }

        if (file.file_in_progress && !file.header_parsed &&
            file.data.size() >= PrimaryHeader::kSize) {
            const PrimaryHeader primary(file.data.data());
            if (file.data.size() >= primary.total_header_length) {
                file.parse_headers();
                file.header_parsed = true;
                if (packet.sequence_flag == 3) {
                    finalize(file, out);
                }
            }
        }
    }
}

}  // namespace ed::xrit
