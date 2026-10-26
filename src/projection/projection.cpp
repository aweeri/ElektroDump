#include "projection/projection.h"

#include <cmath>

#include "projection/geos.h"
#include "projection/reproject.h"
#include "projection/webmercator.h"

namespace ed::projection {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

GeosParams geos_params(const GeoTransform& gt) {
    GeosParams params;
    params.lon0_deg = gt.lon0_deg;
    params.altitude_m = gt.altitude_m;
    return params;
}

// Angular half-extent of the source image in the EW and NS directions.
void source_extent(const GeoTransform& gt, double& half_lon_deg,
                   double& half_lat_deg) {
    double lon_min = 0.0;
    double lon_max = 0.0;
    double lat_min = 0.0;
    double lat_max = 0.0;
    source_bounds(gt, lon_min, lon_max, lat_min, lat_max);
    half_lon_deg = (lon_max - lon_min) / 2.0;
    half_lat_deg = (lat_max - lat_min) / 2.0;
}

PixelMap mercator_map(const GeoTransform& gt, int out_w, int out_h) {
    double half_lon = 0.0;
    double half_lat = 0.0;
    source_extent(gt, half_lon, half_lat);
    const double lon_min = gt.lon0_deg - half_lon;
    const double lon_max = gt.lon0_deg + half_lon;
    const double lat_min = -half_lat;
    const double lat_max = half_lat;
    const double x_min = mercator_x(lon_min);
    const double x_max = mercator_x(lon_max);
    const double y_min = mercator_y(lat_min);
    const double y_max = mercator_y(lat_max);
    const GeosParams params = geos_params(gt);
    return [=](int ox, int oy, double& sx, double& sy) {
        const double x = x_min + (ox + 0.5) / out_w * (x_max - x_min);
        const double y = y_max - (oy + 0.5) / out_h * (y_max - y_min);
        const double lon = mercator_lon(x);
        const double lat = mercator_lat(y);
        double x_rad = 0.0;
        double y_rad = 0.0;
        if (!geos_forward(lat, lon, params, x_rad, y_rad)) {
            return false;
        }
        sx = x_rad * gt.column_scalar + gt.column_offset;
        sy = y_rad * gt.line_scalar + gt.line_offset;
        return true;
    };
}

PixelMap equirect_map(const GeoTransform& gt, int out_w, int out_h) {
    double half_lon = 0.0;
    double half_lat = 0.0;
    source_extent(gt, half_lon, half_lat);
    const double lon_min = gt.lon0_deg - half_lon;
    const double lon_max = gt.lon0_deg + half_lon;
    const double lat_min = -half_lat;
    const double lat_max = half_lat;
    const GeosParams params = geos_params(gt);
    return [=](int ox, int oy, double& sx, double& sy) {
        const double lon = lon_min + (ox + 0.5) / out_w * (lon_max - lon_min);
        const double lat = lat_max - (oy + 0.5) / out_h * (lat_max - lat_min);
        double x_rad = 0.0;
        double y_rad = 0.0;
        if (!geos_forward(lat, lon, params, x_rad, y_rad)) {
            return false;
        }
        sx = x_rad * gt.column_scalar + gt.column_offset;
        sy = y_rad * gt.line_scalar + gt.line_offset;
        return true;
    };
}

}  // namespace

GeoTransform geotransform_from_navigation(const xrit::ImageNavigationRecord& nav,
                                          int width, int height,
                                          double subsatellite_lon_deg) {
    GeoTransform gt;
    gt.lon0_deg = subsatellite_lon_deg;
    gt.altitude_m = 35786023.0;
    gt.sweep_x = false;
    gt.column_scalar = static_cast<double>(nav.column_scaling_factor);
    gt.line_scalar = static_cast<double>(nav.line_scaling_factor);
    gt.column_offset = nav.column_offset;
    gt.line_offset = nav.line_offset;
    gt.src_width = width;
    gt.src_height = height;
    return gt;
}

void source_bounds(const GeoTransform& gt, double& lon_min, double& lon_max,
                   double& lat_min, double& lat_max) {
    const double col = gt.column_scalar != 0.0 ? gt.column_scalar : 1.0;
    const double line = gt.line_scalar != 0.0 ? gt.line_scalar : 1.0;
    const double x_max = (gt.src_width / 2.0) / std::fabs(col);
    const double y_max = (gt.src_height / 2.0) / std::fabs(line);
    const GeosParams params = geos_params(gt);
    const double max_angle =
        std::acos(params.r_eq / (params.r_eq + params.altitude_m)) / kDegToRad;
    lon_min = gt.lon0_deg - max_angle;
    lon_max = gt.lon0_deg + max_angle;
    lat_min = -max_angle;
    lat_max = max_angle;
    double lat = 0.0;
    double lon = 0.0;
    if (geos_inverse(-x_max, 0.0, params, lat, lon)) {
        lon_min = lon;
    }
    if (geos_inverse(x_max, 0.0, params, lat, lon)) {
        lon_max = lon;
    }
    if (geos_inverse(0.0, -y_max, params, lat, lon)) {
        lat_min = lat;
    }
    if (geos_inverse(0.0, y_max, params, lat, lon)) {
        lat_max = lat;
    }
}

GeoTransform default_geotransform(int width, int height,
                                  double subsatellite_lon_deg) {
    GeoTransform gt;
    gt.lon0_deg = subsatellite_lon_deg;
    gt.altitude_m = 35786023.0;
    gt.sweep_x = false;
    gt.src_width = width;
    gt.src_height = height;
    const double r_eq = 6378137.0;
    const double theta = std::asin(r_eq / (r_eq + gt.altitude_m));
    const double half_w = width / 2.0;
    const double half_h = height / 2.0;
    gt.column_scalar = half_w / theta;
    gt.line_scalar = -half_h / theta;
    gt.column_offset = half_w;
    gt.line_offset = half_h;
    return gt;
}

image::Image8 Projector::reproject_mercator(const image::Image8& src,
                                            const GeoTransform& gt, int out_w,
                                            int out_h) const {
    return resample_rgb(src, out_w, out_h, mercator_map(gt, out_w, out_h));
}

image::ImageRGBA Projector::reproject_mercator_rgba(
    const image::ImageRGBA& src, const GeoTransform& gt, int out_w,
    int out_h) const {
    return resample_rgba(src, out_w, out_h, mercator_map(gt, out_w, out_h));
}

image::Image8 Projector::reproject_equirect(const image::Image8& src,
                                            const GeoTransform& gt, int out_w,
                                            int out_h) const {
    return resample_rgb(src, out_w, out_h, equirect_map(gt, out_w, out_h));
}

image::ImageRGBA Projector::reproject_equirect_rgba(
    const image::ImageRGBA& src, const GeoTransform& gt, int out_w,
    int out_h) const {
    return resample_rgba(src, out_w, out_h, equirect_map(gt, out_w, out_h));
}

}  // namespace ed::projection
