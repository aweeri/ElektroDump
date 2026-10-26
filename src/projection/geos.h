// ElektroDump — geostationary forward/inverse (spec §2.3).
#pragma once

namespace ed::projection {

struct GeosParams {
    double lon0_deg = 76.0;          // sub-satellite longitude
    double altitude_m = 35786023.0;  // nominal GEO altitude
    double r_eq = 6378137.0;         // equatorial radius
    double r_pol = 6356752.31414;    // polar radius
};

// lat/lon (degrees) -> scan angles (radians); false when not visible from the satellite.
bool geos_forward(double lat_deg, double lon_deg, const GeosParams& params,
                  double& x_rad, double& y_rad);

// scan angles (radians) -> lat/lon (degrees); false when the line of sight misses Earth.
bool geos_inverse(double x_rad, double y_rad, const GeosParams& params,
                  double& lat_deg, double& lon_deg);

}  // namespace ed::projection
