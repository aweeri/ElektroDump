// ElektroDump — geostationary projection + reprojection (spec §2.3).
#pragma once

#include "image/image.h"
#include "xrit/xrit_headers.h"

namespace ed::projection {

struct GeoTransform {
    double lon0_deg = 76.0;        // sub-satellite longitude
    double altitude_m = 35786023.0;
    bool sweep_x = false;          // Elektro MSU-GS: false
    double column_scalar = 0.0;    // derived from the navigation record
    double line_scalar = 0.0;
    double column_offset = 0.0;
    double line_offset = 0.0;
    int src_width = 0;
    int src_height = 0;
};

// Derive a GeoTransform from an xRIT ImageNavigationRecord + image size.
GeoTransform geotransform_from_navigation(const xrit::ImageNavigationRecord& nav,
                                          int width, int height,
                                          double subsatellite_lon_deg);

// Fallback when a channel has no navigation record: assume the full image spans
// the Earth's angular diameter.
GeoTransform default_geotransform(int width, int height,
                                  double subsatellite_lon_deg);

// Lon/lat bounding box of the source image (degrees).
void source_bounds(const GeoTransform& gt, double& lon_min, double& lon_max,
                   double& lat_min, double& lat_max);

class Projector {
public:
    image::Image8 reproject_mercator(const image::Image8& src,
                                     const GeoTransform& gt, int out_w,
                                     int out_h) const;
    image::ImageRGBA reproject_mercator_rgba(const image::ImageRGBA& src,
                                             const GeoTransform& gt, int out_w,
                                             int out_h) const;
    image::Image8 reproject_equirect(const image::Image8& src,
                                     const GeoTransform& gt, int out_w,
                                     int out_h) const;
    image::ImageRGBA reproject_equirect_rgba(const image::ImageRGBA& src,
                                             const GeoTransform& gt, int out_w,
                                             int out_h) const;
};

}  // namespace ed::projection
