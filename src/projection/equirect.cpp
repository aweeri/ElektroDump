#include "projection/equirect.h"

namespace ed::projection {

double equirect_x(double lon_deg) { return lon_deg; }
double equirect_y(double lat_deg) { return lat_deg; }
double equirect_lon(double x) { return x; }
double equirect_lat(double y) { return y; }

}  // namespace ed::projection
