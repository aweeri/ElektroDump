#include "ccsds/aos_demux.h"

#include <algorithm>

#include "ccsds/vcdu.h"

namespace ed::ccsds {

namespace {

constexpr uint16_t kNoHeader = 2047;
constexpr std::size_t kPacketHeader = 6;
constexpr std::size_t kMaxPayload = 65536;  // sanity cap

}  // namespace

void AosDemuxer::reset() {
    active_ = false;
    current_ = CcsdsPacket{};
    remaining_ = 0;
    header_carry_.clear();
}

void AosDemuxer::emit(std::vector<CcsdsPacket>& out) {
    out.push_back(current_);
    active_ = false;
    current_ = CcsdsPacket{};
    remaining_ = 0;
}

void AosDemuxer::feed_continuation(const uint8_t* data, std::size_t n,
                                   std::vector<CcsdsPacket>& out) {
    if (!active_) {
        return;
    }
    const std::size_t take = std::min(n, remaining_);
    current_.payload.insert(current_.payload.end(), data, data + take);
    remaining_ -= take;
    if (remaining_ == 0) {
        emit(out);
    }
}

void AosDemuxer::push(const uint8_t* cadu, std::size_t len,
                      std::vector<CcsdsPacket>& out) {
    if (cadu == nullptr || len < kMpduDataOffset) {
        return;
    }
    const std::size_t field = std::min(mpdu_size_, len - kMpduDataOffset);
    const uint8_t* data = cadu + kMpduDataOffset;
    const uint16_t fhp = vcdu_first_header_pointer(cadu, len);
    const bool fhp_valid = (fhp < field);

    std::size_t pos = 0;
    if (!fhp_valid) {
        // The whole data field continues the packet in progress.
        feed_continuation(data, field, out);
        return;
    }

    if (fhp > 0) {
        feed_continuation(data, fhp, out);
        if (active_) {
            // The continued packet should have ended before the new header.
            active_ = false;
            current_ = CcsdsPacket{};
            remaining_ = 0;
        }
    }
    pos = fhp;

    while (pos < field) {
        if (!active_) {
            if (header_carry_.size() + (field - pos) < kPacketHeader) {
                header_carry_.insert(header_carry_.end(), data + pos,
                                     data + field);
                break;
            }
            uint8_t header[kPacketHeader];
            std::size_t hn = 0;
            while (hn < kPacketHeader && !header_carry_.empty()) {
                header[hn++] = header_carry_.front();
                header_carry_.erase(header_carry_.begin());
            }
            while (hn < kPacketHeader) {
                header[hn++] = data[pos++];
            }
            current_ = CcsdsPacket{};
            current_.apid = static_cast<uint16_t>(
                ((header[0] & 0x07u) << 8) | header[1]);
            current_.sequence_flag = static_cast<uint8_t>(header[2] >> 6);
            current_.packet_sequence_count = static_cast<uint16_t>(
                ((header[2] & 0x3Fu) << 8) | header[3]);
            current_.packet_length = static_cast<uint16_t>(
                (static_cast<uint16_t>(header[4]) << 8) | header[5]);
            remaining_ = static_cast<std::size_t>(current_.packet_length) + 1;
            if (remaining_ > kMaxPayload) {
                remaining_ = 0;  // malformed header; drop this packet
                continue;
            }
            active_ = true;
        }

        const std::size_t take = std::min(field - pos, remaining_);
        current_.payload.insert(current_.payload.end(), data + pos,
                                data + pos + take);
        pos += take;
        remaining_ -= take;
        if (remaining_ == 0) {
            emit(out);
        }
    }
}

}  // namespace ed::ccsds
