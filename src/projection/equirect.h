// ElektroDump — equirectangular forward/inverse (spec §2.3).
#pragma once

namespace ed::projection {

// Equirectangular coordinates are plain degrees; the caller scales them to the grid.
double equirect_x(double lon_deg);
double equirect_y(double lat_deg);
double equirect_lon(double x);
double equirect_lat(double y);

}  // namespace ed::projection
