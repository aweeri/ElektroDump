// ElektroDump — product metadata shared by the M6 builder and the M7 store.
#pragma once

#include <string>

namespace ed::output {

// Georeferencing of a projected product (spec §2.8). Optional: `valid` is
// false for unprojected ("none") products.
struct GeoReference {
    bool valid = false;
    double lon0_deg = 0.0;
    double altitude_m = 0.0;
    double lon_min = 0.0;
    double lon_max = 0.0;
    double lat_min = 0.0;
    double lat_max = 0.0;
};

struct ProductMeta {
    std::string id;          // "<UTC>_<product>"
    std::string satellite;   // "elektro-l3"
    std::string product;     // "visual" | "ir" | "ir_cloud" | "ir_cloud_map"
    std::string projection;  // "mercator" | "equirect" | "none"
    int width = 0;
    int height = 0;
    std::string capture_start;
    std::string capture_end;
    std::string created;  // ISO-8601 UTC
    bool partial = false;
    bool crc_recovered = false;
    std::string path;  // relative to storage root
    std::string ext;   // "webp" | "png" | "ppm"
    GeoReference geo;  // optional georeferencing
};

}  // namespace ed::output
