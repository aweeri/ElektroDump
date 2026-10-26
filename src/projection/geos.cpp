#include "projection/geos.h"

#include <cmath>

namespace ed::projection {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

}  // namespace

bool geos_forward(double lat_deg, double lon_deg, const GeosParams& params,
                  double& x_rad, double& y_rad) {
    const double lat = lat_deg * kDegToRad;
    const double lon = (lon_deg - params.lon0_deg) * kDegToRad;
    const double r_eq = params.r_eq;
    const double r_pol = params.r_pol;
    const double e2 = 1.0 - (r_pol * r_pol) / (r_eq * r_eq);
    const double sin_lat = std::sin(lat);
    const double cos_lat = std::cos(lat);
    const double n = r_eq / std::sqrt(1.0 - e2 * sin_lat * sin_lat);
    const double px = n * cos_lat * std::cos(lon);
    const double py = n * cos_lat * std::sin(lon);
    const double pz = n * (1.0 - e2) * sin_lat;
    const double h = r_eq + params.altitude_m;
    const double vx = px - h;
    const double vy = py;
    const double vz = pz;
    // The point must be in front of the satellite and above the local horizon
    // (the far side of the Earth is occluded).
    if (vx >= 0.0 || px <= (r_eq * r_eq) / h) {
        return false;
    }
    x_rad = std::atan2(vy, -vx);
    y_rad = std::atan2(vz, std::sqrt(vx * vx + vy * vy));
    return true;
}

bool geos_inverse(double x_rad, double y_rad, const GeosParams& params,
                  double& lat_deg, double& lon_deg) {
    const double r_eq = params.r_eq;
    const double r_pol = params.r_pol;
    const double h = r_eq + params.altitude_m;
    const double cosx = std::cos(x_rad);
    const double sinx = std::sin(x_rad);
    const double cosy = std::cos(y_rad);
    const double siny = std::sin(y_rad);
    const double a = cosy * cosy + siny * siny * (r_eq * r_eq) / (r_pol * r_pol);
    const double b = -2.0 * h * cosx * cosy;
    const double c = h * h - r_eq * r_eq;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) {
        return false;
    }
    const double t = (-b - std::sqrt(disc)) / (2.0 * a);
    if (t <= 0.0) {
        return false;
    }
    const double px = h - t * cosx * cosy;
    const double py = t * sinx * cosy;
    const double pz = t * siny;
    const double r_xy = std::sqrt(px * px + py * py);
    const double e2 = 1.0 - (r_pol * r_pol) / (r_eq * r_eq);
    lat_deg = std::atan2(pz, (1.0 - e2) * r_xy) * kRadToDeg;
    lon_deg = std::atan2(py, px) * kRadToDeg + params.lon0_deg;
    return true;
}

}  // namespace ed::projection
