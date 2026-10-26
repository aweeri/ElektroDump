// ElektroDump — M7b self-tests (WebP/PNG/PPM encoding).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "image/image.h"
#include "output/webp_writer.h"
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

ed::image::Image8 make_rgb() {
    ed::image::Image8 image;
    image.resize(4, 4, 3);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 4 + x) * 3;
            image.data[i] = static_cast<uint8_t>(x * 60);
            image.data[i + 1] = static_cast<uint8_t>(y * 60);
            image.data[i + 2] = 128;
        }
    }
    return image;
}

ed::image::ImageRGBA make_rgba() {
    ed::image::ImageRGBA image;
    image.resize(4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 4 + x) * 4;
            image.data[i] = static_cast<uint8_t>(x * 60);
            image.data[i + 1] = static_cast<uint8_t>(y * 60);
            image.data[i + 2] = 128;
            image.data[i + 3] = static_cast<uint8_t>(x * 60);
        }
    }
    return image;
}

bool is_webp(const std::string& bytes) {
    return bytes.size() > 12 && bytes.compare(0, 4, "RIFF") == 0 &&
           bytes.compare(8, 4, "WEBP") == 0;
}

bool is_png(const std::string& bytes) {
    return bytes.size() > 8 &&
           static_cast<uint8_t>(bytes[0]) == 0x89 &&
           bytes.compare(1, 3, "PNG") == 0;
}

bool is_ppm(const std::string& bytes) {
    return bytes.size() > 2 && bytes.compare(0, 2, "P6") == 0;
}

void test_encode_rgb() {
    const ed::image::Image8 image = make_rgb();
    ed::output::WebpOptions options;
    std::string bytes;
    CHECK(ed::output::encode_webp_rgb(image, options, bytes));
    CHECK(!bytes.empty());
    const std::string format = ed::output::webp_format();
    if (format == "webp") {
        CHECK(is_webp(bytes));
    } else if (format == "png") {
        CHECK(is_png(bytes));
    } else {
        CHECK(is_ppm(bytes));
    }
}

void test_encode_rgba() {
    const ed::image::ImageRGBA image = make_rgba();
    ed::output::WebpOptions options;
    std::string bytes;
    CHECK(ed::output::encode_webp_rgba(image, options, bytes));
    CHECK(!bytes.empty());
    const std::string format = ed::output::webp_format();
    if (format == "webp") {
        CHECK(is_webp(bytes));
    } else if (format == "png") {
        CHECK(is_png(bytes));
    } else {
        CHECK(is_ppm(bytes));
    }
}

void test_write_file() {
    ed::fs::mkdir_p("build");
    const std::string path = "build/m7_webp_test.bin";
    const ed::image::Image8 image = make_rgb();
    ed::output::WebpOptions options;
    CHECK(ed::output::write_webp_rgb(image, path, options));
    CHECK(ed::fs::is_regular_file(path));
    CHECK(ed::fs::file_size(path) > 0);
    ed::fs::remove_all(path);
}

}  // namespace

int main() {
    test_encode_rgb();
    test_encode_rgba();
    test_write_file();

    if (g_failures == 0) {
        std::printf("all M7b webp tests passed (format: %s)\n",
                    ed::output::webp_format());
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
