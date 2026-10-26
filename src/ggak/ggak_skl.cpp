#include "ggak/ggak_skl.h"

#include <cmath>

namespace ed::ggak {

namespace {

// Payload starts after sync[4] + id + master_counter(2) + channel_counter(2) +
// timestamp(4) = 13 bytes.
constexpr std::size_t kPayloadOffset = 13;
constexpr std::size_t kRecordBytes = 14;
constexpr int kRecordCount = 14;

}  // namespace

std::vector<GgakSklRecord> parse_skl(const GgakFrame& frame) {
    std::vector<GgakSklRecord> records;
    if (frame.id != 48) {
        return records;
    }
    records.reserve(kRecordCount);

    for (int i = 0; i < kRecordCount; ++i) {
        const uint8_t* ptr = frame.raw + kPayloadOffset + i * kRecordBytes;
        GgakSklRecord rec;
        rec.time = frame.timestamp_unix;

        for (int c = 0; c < 11; ++c) {
            const double raw = static_cast<double>(ptr[3 + c]);
            if (c == 10) {
                rec.dat[11] = raw * 10.0;
            } else if (c == 0 || c == 9) {
                // Non-linear channels: exp((v - 67) / 29.02) * 1000.
                rec.dat[c] = std::exp((raw - 67.0) / 29.02) * 100.0 * 10.0;
            } else {
                rec.dat[c] = raw * 10.0;
            }
        }
        // Channel 10 is not present in the downlink.
        rec.dat[10] = -9.99999e+99;
        records.push_back(rec);
    }
    return records;
}

std::vector<GgakSklRecord> resample_skl_10s(
    const std::vector<GgakSklRecord>& records) {
    std::vector<GgakSklRecord> out;
    if (records.empty()) {
        return out;
    }
    out.reserve(records.size() / 10 + 1);

    GgakSklRecord acc;
    int count = 0;
    for (const GgakSklRecord& rec : records) {
        if (count == 0) {
            acc = GgakSklRecord{};
            acc.time = rec.time;
        }
        for (int i = 0; i < 12; ++i) {
            acc.dat[i] += rec.dat[i];
        }
        ++count;
        if (count == 10) {
            for (int i = 0; i < 12; ++i) {
                acc.dat[i] /= 10.0;
            }
            out.push_back(acc);
            count = 0;
        }
    }
    return out;
}

}  // namespace ed::ggak
