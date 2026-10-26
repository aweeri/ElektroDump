// ElektroDump — M6b self-tests (calibration LUT + palette fallbacks).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "image/calibration.h"
#include "image/lut.h"
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

std::string make_table_json() {
    std::string json = "{\"version\":1,\"channels\":[";
    json += "{\"id\":0,\"wavelength_um\":0.5,\"unit\":\"reflectance\",\"lut\":[";
    for (int i = 0; i < 1024; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += std::to_string(static_cast<double>(i) / 1023.0);
    }
    json += "]},";
    json +=
        "{\"id\":3,\"wavelength_um\":10.7,\"unit\":\"brightness_temperature\","
        "\"lut\":[";
    for (int i = 0; i < 1024; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += std::to_string(190.0 + static_cast<double>(i) / 1023.0 * 130.0);
    }
    json += "]}]}";
    return json;
}

void test_load_and_units() {
    const std::string path = "build/m6_cal_test.json";
    ed::fs::mkdir_p("build");
    CHECK(ed::fs::write_file(path, make_table_json()));

    std::string error;
    const ed::image::CalibrationTable table =
        ed::image::load_calibration(path, &error);
    CHECK(error.empty());
    CHECK(table.valid);
    CHECK(table.lut.size() == 4);
    CHECK(table.lut[0].size() == 1024);
    CHECK(table.lut[3].size() == 1024);
    CHECK(std::fabs(table.wavelengths_um[0] - 0.5) < 1e-9);
    CHECK(std::fabs(table.wavelengths_um[3] - 10.7) < 1e-9);
    CHECK(table.units[0] == ed::image::CalUnit::Reflectance);
    CHECK(table.units[3] == ed::image::CalUnit::BrightnessTemperature);
    CHECK(std::fabs(table.wavenumber_cm1[3] - 1.0e4 / 10.7) < 1e-6);
}

void test_calibrate_reflectance() {
    std::string error;
    const ed::image::CalibrationTable table =
        ed::image::load_calibration("build/m6_cal_test.json", &error);
    CHECK(table.valid);

    ed::image::Image16 raw;
    raw.resize(2, 2, 1);
    raw.data = {0, 100, 500, 1024};
    ed::image::Mask valid;
    valid.resize(2, 2);
    valid.data = {1, 1, 1, 1};

    ed::image::Mask out_valid;
    const ed::image::Image16 out =
        ed::image::calibrate(raw, valid, table, 0, &out_valid);
    CHECK(out_valid.data[0] == 0);  // raw 0 is invalid
    CHECK(out_valid.data[1] == 1);
    CHECK(out_valid.data[2] == 1);
    CHECK(out_valid.data[3] == 0);  // raw > 1023 is invalid
    const uint16_t expected =
        static_cast<uint16_t>(100.0 / 1023.0 * 65535.0 + 0.5);
    CHECK(out.data[1] == expected);
    CHECK(out.data[0] == 0);
    CHECK(out.data[3] == 0);
}

void test_calibrate_temperature() {
    std::string error;
    const ed::image::CalibrationTable table =
        ed::image::load_calibration("build/m6_cal_test.json", &error);
    CHECK(table.valid);

    ed::image::Image16 raw;
    raw.resize(1, 1, 1);
    raw.data = {500};
    ed::image::Mask valid;
    valid.resize(1, 1);
    valid.data = {1};

    ed::image::Mask out_valid;
    const ed::image::Image16 out =
        ed::image::calibrate(raw, valid, table, 3, &out_valid);
    CHECK(out_valid.data[0] == 1);
    const uint16_t expected =
        static_cast<uint16_t>(190.0 + 500.0 / 1023.0 * 130.0 + 0.5);
    CHECK(out.data[0] == expected);
}

void test_missing_table_passthrough() {
    std::string error;
    const ed::image::CalibrationTable table =
        ed::image::load_calibration("build/does_not_exist.json", &error);
    CHECK(!table.valid);
    CHECK(!error.empty());

    ed::image::Image16 raw;
    raw.resize(1, 2, 1);
    raw.data = {0, 700};
    ed::image::Mask valid;
    valid.resize(1, 2);
    valid.data = {1, 0};

    ed::image::Mask out_valid;
    const ed::image::Image16 out =
        ed::image::calibrate(raw, valid, table, 0, &out_valid);
    CHECK(out.data[0] == 0);
    CHECK(out.data[1] == 700);
    CHECK(out_valid.data[0] == 1);
    CHECK(out_valid.data[1] == 0);
}

void test_lut_fallbacks() {
    ed::image::Lut1D lut1d;
    CHECK(lut1d.load("build/does_not_exist.png"));
    CHECK(lut1d.loaded());
    uint8_t rgb[3] = {0, 0, 0};
    lut1d.sample(0.0, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    lut1d.sample(1.0, rgb);
    CHECK(rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255);
    lut1d.sample(0.5, rgb);
    CHECK(rgb[0] > 100 && rgb[0] < 160);

    ed::image::Lut2D lut2d;
    CHECK(!lut2d.load("build/does_not_exist.png"));
    CHECK(!lut2d.loaded());
}

}  // namespace

int main() {
    test_load_and_units();
    test_calibrate_reflectance();
    test_calibrate_temperature();
    test_missing_table_passthrough();
    test_lut_fallbacks();

    if (g_failures == 0) {
        std::printf("all M6b calibration tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
