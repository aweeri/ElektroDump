// ElektroDump — Web Mercator forward/inverse (spec §2.3).
#pragma once

namespace ed::projection {

// Earth radius used by Web Mercator (EPSG:3857).
constexpr double kMercatorRadius = 6378137.0;

// Forward: lon/lat in degrees -> metres.
double mercator_x(double lon_deg);
double mercator_y(double lat_deg);

// Inverse: metres -> lon/lat in degrees.
double mercator_lon(double x);
double mercator_lat(double y);

}  // namespace ed::projection
