// ElektroDump — M6a self-tests (xRIT segment stitching).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/config.h"
#include "image/image_assembler.h"
#include "xrit/xrit_file.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                         #cond);                                         \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

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

// Build a complete xRIT image-segment file (headers + data field).
ed::xrit::XritFile make_segment(uint16_t sc_id, uint8_t channel, uint16_t seq,
                                uint16_t start, uint16_t end, uint16_t columns,
                                uint16_t lines, uint8_t bpp,
                                const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> headers;
    // Primary header (total_header_length patched below).
    headers.push_back(0);
    put_be16(headers, 16);
    headers.push_back(0);  // file_type_code = image
    const std::size_t total_pos = headers.size();
    put_be32(headers, 0);
    put_be64(headers, payload.size());

    // Image structure.
    headers.push_back(1);
    put_be16(headers, 9);
    headers.push_back(bpp);
    put_be16(headers, columns);
    put_be16(headers, lines);
    headers.push_back(0);

    // Segment identification.
    headers.push_back(128);
    put_be16(headers, 13);
    put_be16(headers, sc_id);
    headers.push_back(channel);
    put_be16(headers, seq);
    put_be16(headers, start);
    put_be16(headers, end);
    headers.push_back(0);

    // Timestamp (CCSDS days + ms of day).
    headers.push_back(5);
    put_be16(headers, 9);
    put_be16(headers, 20000);
    put_be32(headers, 3600000);

    // Annotation (filename).
    const std::string name = "SEG_" + std::to_string(channel) + "_" +
                             std::to_string(seq) + ".lrit";
    headers.push_back(4);
    put_be16(headers, static_cast<uint16_t>(3 + name.size()));
    headers.insert(headers.end(), name.begin(), name.end());

    const uint32_t total = static_cast<uint32_t>(headers.size());
    headers[total_pos] = static_cast<uint8_t>((total >> 24) & 0xFF);
    headers[total_pos + 1] = static_cast<uint8_t>((total >> 16) & 0xFF);
    headers[total_pos + 2] = static_cast<uint8_t>((total >> 8) & 0xFF);
    headers[total_pos + 3] = static_cast<uint8_t>(total & 0xFF);

    ed::xrit::XritFile file;
    file.data = headers;
    file.data.insert(file.data.end(), payload.begin(), payload.end());
    file.parse_headers();
    file.header_parsed = true;
    return file;
}

std::vector<uint8_t> make_payload_8bit(int rows, int columns, int base) {
    std::vector<uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(rows) * columns);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            payload.push_back(
                static_cast<uint8_t>((base + r * 3 + c) & 0xFF));
        }
    }
    return payload;
}

void test_full_assembly() {
    ed::Config cfg = ed::Config::defaults();
    ed::image::ImageAssembler assembler(cfg);

    const int columns = 8;
    const int lines = 40;
    const int segments = 4;
    const int segment_height = lines / segments;

    ed::image::AssembledChannel out;
    bool complete = false;
    for (int s = 0; s < segments; ++s) {
        const std::vector<uint8_t> payload =
            make_payload_8bit(segment_height, columns, s * 10);
        const ed::xrit::XritFile file =
            make_segment(0x1AA, 0, static_cast<uint16_t>(s), 0,
                         static_cast<uint16_t>(segments - 1),
                         static_cast<uint16_t>(columns),
                         static_cast<uint16_t>(lines), 8, payload);
        complete = assembler.push(file, out);
    }
    CHECK(complete);
    CHECK(out.image.width == columns);
    CHECK(out.image.height == lines);
    CHECK(out.segments_expected == segments);
    CHECK(out.segments_present == segments);
    CHECK(!out.partial);
    CHECK(out.timestamp_utc == "2012-10-04T01:00:00Z");
    CHECK(out.valid.data.size() ==
          static_cast<std::size_t>(columns) * lines);
    bool all_valid = true;
    for (uint8_t v : out.valid.data) {
        if (v == 0) {
            all_valid = false;
        }
    }
    CHECK(all_valid);
    // Spot-check the first pixel of each segment.
    for (int s = 0; s < segments; ++s) {
        const std::size_t index =
            static_cast<std::size_t>(s * segment_height) * columns;
        CHECK(out.image.data[index] == static_cast<uint16_t>(s * 10));
    }
    const ed::image::ImageAssembler::Stats stats = assembler.stats();
    CHECK(stats.files == static_cast<uint64_t>(segments));
    CHECK(stats.channels == 1);
    CHECK(stats.segments_placed == static_cast<uint64_t>(segments));
}

void test_missing_segment() {
    ed::Config cfg = ed::Config::defaults();
    ed::image::ImageAssembler assembler(cfg);

    const int columns = 4;
    const int lines = 40;
    const int segments = 4;
    const int segment_height = lines / segments;

    // Push segments 0, 1 and 3; segment 2 is missing.
    for (int s : {0, 1, 3}) {
        const std::vector<uint8_t> payload =
            make_payload_8bit(segment_height, columns, s * 10);
        const ed::xrit::XritFile file =
            make_segment(0x1AA, 1, static_cast<uint16_t>(s), 0,
                         static_cast<uint16_t>(segments - 1),
                         static_cast<uint16_t>(columns),
                         static_cast<uint16_t>(lines), 8, payload);
        ed::image::AssembledChannel out;
        CHECK(!assembler.push(file, out));
    }

    std::vector<ed::image::AssembledChannel> flushed;
    assembler.flush(flushed);
    CHECK(flushed.size() == 1);
    if (flushed.size() == 1) {
        const ed::image::AssembledChannel& ch = flushed[0];
        CHECK(ch.segments_present == 3);
        CHECK(ch.segments_expected == 4);
        CHECK(ch.partial);  // 25% missing > 5% default
        // Missing rows (segment 2) must be invalid.
        const std::size_t missing_row =
            static_cast<std::size_t>(2 * segment_height) * columns;
        CHECK(ch.valid.data[missing_row] == 0);
        CHECK(ch.valid.data[0] == 1);
    }
    CHECK(assembler.stats().segments_missing == 1);
}

void test_10bit_unpack() {
    ed::Config cfg = ed::Config::defaults();
    ed::image::ImageAssembler assembler(cfg);

    const int columns = 4;
    const int lines = 2;
    std::vector<uint8_t> payload;
    for (int i = 0; i < columns * lines; ++i) {
        const uint16_t value = static_cast<uint16_t>(100 + i);
        payload.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(value & 0xFF));
    }
    const ed::xrit::XritFile file =
        make_segment(0x1AA, 2, 0, 0, 0, static_cast<uint16_t>(columns),
                     static_cast<uint16_t>(lines), 10, payload);
    ed::image::AssembledChannel out;
    CHECK(assembler.push(file, out));
    CHECK(out.image.data.size() == static_cast<std::size_t>(columns) * lines);
    for (int i = 0; i < columns * lines; ++i) {
        CHECK(out.image.data[static_cast<std::size_t>(i)] ==
              static_cast<uint16_t>(100 + i));
    }
}

void test_non_image_ignored() {
    ed::Config cfg = ed::Config::defaults();
    ed::image::ImageAssembler assembler(cfg);
    ed::xrit::XritFile file;
    file.header_parsed = false;
    ed::image::AssembledChannel out;
    CHECK(!assembler.push(file, out));
}

}  // namespace

int main() {
    test_full_assembly();
    test_missing_segment();
    test_10bit_unpack();
    test_non_image_ignored();

    if (g_failures == 0) {
        std::printf("all M6a image assembler tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
