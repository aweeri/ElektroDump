// ElektroDump — M6c self-tests (composites, alpha blending) + fixture writer.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "image/alpha_blend.h"
#include "image/composite.h"
#include "util/fs_util.h"

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

std::vector<uint8_t> make_segment_file(uint16_t sc_id, uint8_t channel,
                                       uint16_t seq, uint16_t start,
                                       uint16_t end, uint16_t columns,
                                       uint16_t lines,
                                       const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> headers;
    headers.push_back(0);
    put_be16(headers, 16);
    headers.push_back(0);
    const std::size_t total_pos = headers.size();
    put_be32(headers, 0);
    put_be64(headers, payload.size());

    headers.push_back(1);
    put_be16(headers, 9);
    headers.push_back(8);
    put_be16(headers, columns);
    put_be16(headers, lines);
    headers.push_back(0);

    headers.push_back(128);
    put_be16(headers, 13);
    put_be16(headers, sc_id);
    headers.push_back(channel);
    put_be16(headers, seq);
    put_be16(headers, start);
    put_be16(headers, end);
    headers.push_back(0);

    headers.push_back(5);
    put_be16(headers, 9);
    put_be16(headers, 20000);
    put_be32(headers, 3600000);

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

    headers.insert(headers.end(), payload.begin(), payload.end());
    return headers;
}

void test_alpha_over() {
    ed::image::ImageRGBA top;
    top.resize(1, 1);
    top.data = {255, 0, 0, 128};
    ed::image::ImageRGBA bottom;
    bottom.resize(1, 1);
    bottom.data = {0, 0, 255, 0};
    ed::image::ImageRGBA out;
    ed::image::over(top, bottom, out);
    CHECK(out.data[0] == 255);
    CHECK(out.data[1] == 0);
    CHECK(out.data[2] == 0);
    CHECK(out.data[3] == 128);
}

void test_draw_alpha() {
    ed::image::ImageRGBA fg;
    fg.resize(1, 1);
    fg.data = {255, 0, 0, 128};
    ed::image::Image8 bg;
    bg.resize(1, 1, 3);
    bg.data = {0, 0, 255};
    ed::image::draw_alpha(fg, bg);
    CHECK(bg.data[0] >= 126 && bg.data[0] <= 130);
    CHECK(bg.data[1] == 0);
    CHECK(bg.data[2] >= 125 && bg.data[2] <= 129);
}

void test_visual() {
    std::vector<ed::image::Image16> channels(3);
    channels[0].resize(2, 1, 1);
    channels[0].data = {65535, 0};
    channels[1].resize(2, 1, 1);
    channels[1].data = {0, 65535};
    channels[2].resize(2, 1, 1);
    channels[2].data = {32768, 32768};

    ed::image::CompositeConfig config;
    config.calibrated = true;
    const ed::image::Image8 out = ed::image::build_visual(channels, config);
    CHECK(out.width == 2);
    CHECK(out.height == 1);
    CHECK(out.data[0] == 255);
    CHECK(out.data[1] == 0);
    CHECK(out.data[2] >= 127 && out.data[2] <= 129);
    CHECK(out.data[3] == 0);
    CHECK(out.data[4] == 255);
    CHECK(out.data[5] >= 127 && out.data[5] <= 129);
}

void test_ir_monotonic() {
    std::vector<ed::image::Image16> channels(4);
    channels[3].resize(4, 1, 1);
    channels[3].data = {200, 240, 250, 300};

    ed::image::CompositeConfig config;
    config.calibrated = true;
    config.ir_channel = 3;
    config.ir_min_k = 190.0;
    config.ir_max_k = 320.0;
    const ed::image::Image8 out = ed::image::build_ir(channels, config);
    CHECK(out.width == 4);
    for (int i = 1; i < 4; ++i) {
        CHECK(out.data[static_cast<std::size_t>(i) * 3] >=
              out.data[static_cast<std::size_t>(i - 1) * 3]);
    }
}

void test_ir_cloud_alpha() {
    std::vector<ed::image::Image16> channels(4);
    channels[3].resize(5, 1, 1);
    // 0 = off-disk, 200/240 = cold cloud tops, 250 = threshold, 300 = warm.
    channels[3].data = {0, 200, 240, 250, 300};

    ed::image::CompositeConfig config;
    config.calibrated = true;
    config.ir_channel = 3;
    config.ir_min_k = 190.0;
    config.ir_max_k = 320.0;
    config.cloud_threshold_k = 250.0;
    config.cloud_softness_k = 10.0;
    const ed::image::ImageRGBA out = ed::image::build_ir_cloud(channels, config);
    CHECK(out.width == 5);
    CHECK(out.data[0 * 4 + 3] == 0);    // off-disk
    CHECK(out.data[1 * 4 + 3] == 255);  // very cold
    CHECK(out.data[2 * 4 + 3] == 255);  // cold
    CHECK(out.data[3 * 4 + 3] == 0);    // at threshold
    CHECK(out.data[4 * 4 + 3] == 0);    // warm
}

void test_ir_cloud_map() {
    std::vector<ed::image::Image16> channels(4);
    channels[3].resize(2, 1, 1);
    channels[3].data = {200, 300};
    ed::image::CompositeConfig config;
    config.calibrated = true;
    config.ir_channel = 3;
    config.ir_min_k = 190.0;
    config.ir_max_k = 320.0;
    config.cloud_threshold_k = 250.0;
    config.cloud_softness_k = 10.0;

    ed::image::Image8 map;
    map.resize(2, 1, 3);
    map.data = {10, 20, 30, 10, 20, 30};
    const ed::image::Image8 out =
        ed::image::build_ir_cloud_map(channels, config, map);
    CHECK(out.width == 2);
    // Cold pixel is opaque cloud; warm pixel keeps the map underlay.
    CHECK(out.data[3] == 10);
    CHECK(out.data[4] == 20);
    CHECK(out.data[5] == 30);
}

// Write a synthetic full-disk xRIT fixture for the M6 smoke test.
void write_fixture() {
    const char* dir = "build/m6_fixture";
    ed::fs::mkdir_p(dir);
    const int columns = 64;
    const int lines = 64;
    const int segments = 4;
    const int segment_height = lines / segments;
    const uint16_t sc_id = 0x1AA;

    for (int channel = 0; channel < 4; ++channel) {
        for (int segment = 0; segment < segments; ++segment) {
            std::vector<uint8_t> payload;
            payload.reserve(static_cast<std::size_t>(segment_height) * columns);
            for (int row = 0; row < segment_height; ++row) {
                const int y = segment * segment_height + row;
                for (int x = 0; x < columns; ++x) {
                    const int dx = x - columns / 2;
                    const int dy = y - lines / 2;
                    const int distance2 = dx * dx + dy * dy;
                    uint8_t value = 0;
                    if (distance2 <= 28 * 28) {
                        if (channel < 3) {
                            value = static_cast<uint8_t>(
                                100 + (x * 4 + channel * 50) % 800);
                        } else {
                            value = static_cast<uint8_t>(
                                200 + (y * 8) % 800);
                        }
                    }
                    payload.push_back(value);
                }
            }
            const std::vector<uint8_t> file = make_segment_file(
                sc_id, static_cast<uint8_t>(channel),
                static_cast<uint16_t>(segment), 0,
                static_cast<uint16_t>(segments - 1),
                static_cast<uint16_t>(columns), static_cast<uint16_t>(lines),
                payload);
            const std::string path =
                std::string(dir) + "/seg_c" + std::to_string(channel) + "_s" +
                std::to_string(segment) + ".lrit";
            ed::fs::write_file(path, std::string(file.begin(), file.end()));
        }
    }
    std::printf("m6 fixture: %s (%d channels x %d segments)\n", dir, 4,
                segments);
}

}  // namespace

int main() {
    write_fixture();
    test_alpha_over();
    test_draw_alpha();
    test_visual();
    test_ir_monotonic();
    test_ir_cloud_alpha();
    test_ir_cloud_map();

    if (g_failures == 0) {
        std::printf("all M6c composite tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
