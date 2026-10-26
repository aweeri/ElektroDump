#include "projection/webmercator.h"

#include <cmath>

namespace ed::projection {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
// Clamp the latitude so asinh(tan(lat)) stays finite.
constexpr double kMaxLatDeg = 85.05112878;

}  // namespace

double mercator_x(double lon_deg) {
    return kMercatorRadius * lon_deg * kDegToRad;
}

double mercator_y(double lat_deg) {
    double lat = lat_deg;
    if (lat > kMaxLatDeg) {
        lat = kMaxLatDeg;
    } else if (lat < -kMaxLatDeg) {
        lat = -kMaxLatDeg;
    }
    return kMercatorRadius * std::asinh(std::tan(lat * kDegToRad));
}

double mercator_lon(double x) {
    return x / kMercatorRadius * kRadToDeg;
}

double mercator_lat(double y) {
    return std::atan(std::sinh(y / kMercatorRadius)) * kRadToDeg;
}

}  // namespace ed::projection
