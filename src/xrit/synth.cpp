#include "xrit/synth.h"

#include <cstring>
#include <string>

#include "ccsds/vcdu.h"
#include "fec/crc16.h"
#include "fec/derandomizer.h"
#include "fec/reed_solomon.h"
#include "fec/viterbi.h"
#include "xrit/xrit_headers.h"

namespace ed::xrit {

namespace {

constexpr std::size_t kMpduField = 884;   // bytes 12..895
constexpr std::size_t kPacketHeader = 6;
constexpr std::size_t kPayload = kMpduField - kPacketHeader;  // 878 incl CRC
constexpr std::size_t kFirstSkip = 10;

void put_be16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void put_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void put_be64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 7; i >= 0; --i) {
        v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
    }
}

// Build the xRIT file body: header records followed by the data field.
std::vector<uint8_t> build_file(uint16_t sc_id, int channel,
                                std::size_t data_bytes) {
    std::string filename = "SYNTH_" + std::to_string(channel) + ".lrit";

    std::vector<uint8_t> headers;
    // PrimaryHeader (placeholder total/size filled after).
    put_be16(headers, 0);            // not used (we build primary separately)
    (void)sc_id;

    headers.clear();
    headers.push_back(0);
    put_be16(headers, 16);
    headers.push_back(0);            // file_type_code = 0 (image)
    const uint32_t total_placeholder_pos = static_cast<uint32_t>(headers.size());
    put_be32(headers, 0);            // total_header_length (filled later)
    put_be64(headers, data_bytes);

    headers.push_back(1);
    put_be16(headers, 9);
    headers.push_back(8);
    put_be16(headers, 1000);
    put_be16(headers, 1000);
    headers.push_back(0);

    headers.push_back(128);
    put_be16(headers, 13);
    put_be16(headers, sc_id);
    headers.push_back(static_cast<uint8_t>(channel));
    put_be16(headers, 0);
    put_be16(headers, 0);
    put_be16(headers, 0);
    headers.push_back(0);

    headers.push_back(4);
    put_be16(headers, static_cast<uint16_t>(3 + filename.size()));
    headers.insert(headers.end(), filename.begin(), filename.end());

    const uint32_t total = static_cast<uint32_t>(headers.size());
    headers[total_placeholder_pos] = static_cast<uint8_t>((total >> 24) & 0xFF);
    headers[total_placeholder_pos + 1] =
        static_cast<uint8_t>((total >> 16) & 0xFF);
    headers[total_placeholder_pos + 2] =
        static_cast<uint8_t>((total >> 8) & 0xFF);
    headers[total_placeholder_pos + 3] = static_cast<uint8_t>(total & 0xFF);

    std::vector<uint8_t> file = headers;
    for (std::size_t i = 0; i < data_bytes; ++i) {
        file.push_back(static_cast<uint8_t>((i * 7 + static_cast<std::size_t>(channel)) &
                                            0xFF));
    }
    return file;
}

}  // namespace

SynthLrit make_synthetic_lrit(uint16_t apid, int channel,
                              std::size_t data_bytes) {
    SynthLrit result;
    result.apid = apid;
    result.channel = channel;
    result.file = build_file(0x1AA, channel, data_bytes);

    std::vector<std::vector<uint8_t>> payloads;
    std::size_t offset = 0;
    bool first = true;
    while (offset < result.file.size()) {
        const std::size_t prefix = first ? kFirstSkip : 0;
        const std::size_t capacity = kPayload - 2 - prefix;
        const std::size_t take =
            std::min(capacity, result.file.size() - offset);
        std::vector<uint8_t> payload(kPayload, 0);
        std::size_t p = 0;
        if (first) {
            // 10-byte in-payload header (contents ignored by the demux).
            for (int i = 0; i < 10; ++i) {
                payload[p++] = static_cast<uint8_t>(i);
            }
        }
        std::memcpy(payload.data() + p, result.file.data() + offset, take);
        p += take;
        offset += take;
        (void)p;
        // CRC over everything except the final two bytes.
        const uint16_t crc =
            ed::fec::crc16_ccitt(payload.data(), kPayload - 2);
        payload[kPayload - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        payload[kPayload - 1] = static_cast<uint8_t>(crc & 0xFF);
        payloads.push_back(std::move(payload));
        first = false;
    }

    ed::fec::ReedSolomon rs;
    rs.configure(false);
    ed::fec::Derandomizer derand;
    std::vector<uint8_t> all_bits;

    const std::size_t npackets = payloads.size();
    for (std::size_t idx = 0; idx < npackets; ++idx) {
        uint8_t flag = 0;
        if (npackets == 1) {
            flag = 3;
        } else if (idx == 0) {
            flag = 1;
        } else if (idx + 1 == npackets) {
            flag = 2;
        }

        std::vector<uint8_t> packet(kPacketHeader + kPayload, 0);
        const uint16_t seq = static_cast<uint16_t>(idx & 0x3FFF);
        const uint16_t packet_length = static_cast<uint16_t>(kPayload - 1);
        packet[0] = static_cast<uint8_t>((apid >> 8) & 0x07);
        packet[1] = static_cast<uint8_t>(apid & 0xFF);
        packet[2] = static_cast<uint8_t>(((flag & 0x3) << 6) | ((seq >> 8) & 0x3F));
        packet[3] = static_cast<uint8_t>(seq & 0xFF);
        packet[4] = static_cast<uint8_t>((packet_length >> 8) & 0xFF);
        packet[5] = static_cast<uint8_t>(packet_length & 0xFF);
        std::memcpy(packet.data() + kPacketHeader, payloads[idx].data(), kPayload);

        std::vector<uint8_t> cadu(1024, 0);
        cadu[0] = 0x1A;
        cadu[1] = 0xCF;
        cadu[2] = 0xFC;
        cadu[3] = 0x1D;
        const uint16_t scid = 0x1AA;
        const uint8_t vcid = 0;
        cadu[4] = static_cast<uint8_t>((scid >> 2) & 0x3F);
        cadu[5] = static_cast<uint8_t>(((scid & 0x3) << 6) | (vcid & 0x3F));
        const uint32_t counter = static_cast<uint32_t>(idx);
        cadu[6] = static_cast<uint8_t>((counter >> 16) & 0xFF);
        cadu[7] = static_cast<uint8_t>((counter >> 8) & 0xFF);
        cadu[8] = static_cast<uint8_t>(counter & 0xFF);
        cadu[9] = 0;
        cadu[10] = 0;
        cadu[11] = 0;
        std::memcpy(cadu.data() + ccsds::kMpduDataOffset, packet.data(),
                    packet.size());

        // RS-encode the body region [4, 1024).
        rs.encode_interleaved(cadu.data() + 4, 1020, 4);
        // Randomise for transmission / post-deframer representation.
        std::vector<uint8_t> wire = cadu;
        derand.apply(wire.data() + 4, 1020);
        result.wire_cadus.push_back(wire);

        // Collect CADU bits (ASM included); the encoder runs continuously over
        // the whole capture, like the real link.
        for (std::size_t i = 0; i < wire.size(); ++i) {
            for (int b = 0; b < 8; ++b) {
                all_bits.push_back(
                    static_cast<uint8_t>((wire[i] >> (7 - b)) & 1));
            }
        }
    }

    ed::fec::Viterbi::encode(all_bits.data(), all_bits.size(), 0171, 0133,
                             result.tx_bits);
    return result;
}

}  // namespace ed::xrit
