// ElektroDump — M7a self-tests (geostationary + Web Mercator projection).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "projection/geos.h"
#include "projection/projection.h"
#include "projection/webmercator.h"

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

void test_geos_roundtrip() {
    ed::projection::GeosParams params;
    params.lon0_deg = 76.0;
    const double lats[] = {0.0, 10.0, -10.0, 30.0, -30.0};
    const double lons[] = {76.0, 80.0, 70.0, 90.0, 60.0};
    for (double lat : lats) {
        for (double lon : lons) {
            double x = 0.0;
            double y = 0.0;
            CHECK(ed::projection::geos_forward(lat, lon, params, x, y));
            double lat2 = 0.0;
            double lon2 = 0.0;
            CHECK(ed::projection::geos_inverse(x, y, params, lat2, lon2));
            CHECK(std::fabs(lat - lat2) < 1e-6);
            CHECK(std::fabs(lon - lon2) < 1e-6);
        }
    }
}

void test_geos_off_disk() {
    ed::projection::GeosParams params;
    params.lon0_deg = 76.0;
    double x = 0.0;
    double y = 0.0;
    // The antipode is not visible from the satellite.
    CHECK(!ed::projection::geos_forward(0.0, -104.0, params, x, y));
}

void test_mercator_roundtrip() {
    const double lons[] = {-180.0, -90.0, 0.0, 76.0, 180.0};
    const double lats[] = {-80.0, -45.0, 0.0, 45.0, 80.0};
    for (double lon : lons) {
        for (double lat : lats) {
            const double x = ed::projection::mercator_x(lon);
            const double y = ed::projection::mercator_y(lat);
            CHECK(std::fabs(ed::projection::mercator_lon(x) - lon) < 1e-9);
            CHECK(std::fabs(ed::projection::mercator_lat(y) - lat) < 1e-9);
        }
    }
}

void test_reproject() {
    // A 64x64 source with a bright disk in the centre.
    ed::image::Image8 src;
    src.resize(64, 64, 3);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const int dx = x - 32;
            const int dy = y - 32;
            const bool inside = dx * dx + dy * dy <= 28 * 28;
            const std::size_t i =
                (static_cast<std::size_t>(y) * 64 + x) * 3;
            src.data[i] = inside ? 200 : 0;
            src.data[i + 1] = inside ? 100 : 0;
            src.data[i + 2] = inside ? 50 : 0;
        }
    }
    const ed::projection::GeoTransform gt =
        ed::projection::default_geotransform(64, 64, 76.0);
    ed::projection::Projector projector;
    const ed::image::Image8 out =
        projector.reproject_mercator(src, gt, 128, 128);
    CHECK(out.width == 128);
    CHECK(out.height == 128);
    // The centre of the output should be inside the disk.
    const std::size_t centre = (64 * 128 + 64) * 3;
    CHECK(out.data[centre] > 100);
    // The corners should be empty.
    CHECK(out.data[0] == 0);
    CHECK(out.data[(127 * 128 + 127) * 3] == 0);

    // The disk is present in both axes. Mercator stretches the latitude, so
    // the disk is taller than it is wide; only sanity-check the presence.
    int row_lit = 0;
    int col_lit = 0;
    for (int x = 0; x < 128; ++x) {
        if (out.data[(64 * 128 + x) * 3] > 100) {
            ++row_lit;
        }
    }
    for (int y = 0; y < 128; ++y) {
        if (out.data[(y * 128 + 64) * 3] > 100) {
            ++col_lit;
        }
    }
    CHECK(row_lit > 0);
    CHECK(col_lit > 0);
    const double aspect = static_cast<double>(row_lit) /
                          static_cast<double>(col_lit);
    CHECK(aspect > 0.4 && aspect < 2.5);
}

void test_reproject_equirect() {
    ed::image::Image8 src;
    src.resize(32, 32, 3);
    for (std::size_t i = 0; i < src.data.size(); ++i) {
        src.data[i] = 128;
    }
    const ed::projection::GeoTransform gt =
        ed::projection::default_geotransform(32, 32, 76.0);
    ed::projection::Projector projector;
    const ed::image::Image8 out =
        projector.reproject_equirect(src, gt, 64, 64);
    CHECK(out.width == 64);
    CHECK(out.height == 64);
    // The centre is on-disk and keeps the source colour.
    const std::size_t centre = (32 * 64 + 32) * 3;
    CHECK(out.data[centre] == 128);
}

}  // namespace

int main() {
    test_geos_roundtrip();
    test_geos_off_disk();
    test_mercator_roundtrip();
    test_reproject();
    test_reproject_equirect();

    if (g_failures == 0) {
        std::printf("all M7a projection tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
