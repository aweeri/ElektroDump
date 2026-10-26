#include "output/metadata.h"

#include "util/json_util.h"

namespace ed::output {

std::string metadata_to_json(const ProductMeta& meta) {
    json::Value root;
    root.set("id", json::Value(meta.id));
    root.set("satellite", json::Value(meta.satellite));
    root.set("product", json::Value(meta.product));
    root.set("projection", json::Value(meta.projection));
    root.set("width", json::Value(static_cast<int64_t>(meta.width)));
    root.set("height", json::Value(static_cast<int64_t>(meta.height)));
    root.set("capture_start", json::Value(meta.capture_start));
    root.set("capture_end", json::Value(meta.capture_end));
    root.set("created", json::Value(meta.created));
    root.set("partial", json::Value(meta.partial));
    root.set("crc_recovered", json::Value(meta.crc_recovered));
    root.set("path", json::Value(meta.path));
    root.set("ext", json::Value(meta.ext));
    if (meta.geo.valid) {
        json::Value geo;
        geo.set("lon0_deg", json::Value(meta.geo.lon0_deg));
        geo.set("altitude_m", json::Value(meta.geo.altitude_m));
        geo.set("lon_min", json::Value(meta.geo.lon_min));
        geo.set("lon_max", json::Value(meta.geo.lon_max));
        geo.set("lat_min", json::Value(meta.geo.lat_min));
        geo.set("lat_max", json::Value(meta.geo.lat_max));
        root.set("georeferencing", geo);
    }
    return root.dump(2);
}

bool metadata_from_json(const std::string& text, ProductMeta& meta) {
    std::string error;
    const json::Value root = json::Value::parse(text, &error);
    if (!error.empty() || !root.is_object()) {
        return false;
    }
    const auto get_string = [&root](const char* key) -> std::string {
        const json::Value* value = root.find(key);
        return value != nullptr ? value->as_string() : std::string();
    };
    const auto get_int = [&root](const char* key) -> int {
        const json::Value* value = root.find(key);
        return value != nullptr ? static_cast<int>(value->as_int()) : 0;
    };
    const auto get_bool = [&root](const char* key) -> bool {
        const json::Value* value = root.find(key);
        return value != nullptr ? value->as_bool() : false;
    };
    meta.id = get_string("id");
    meta.satellite = get_string("satellite");
    meta.product = get_string("product");
    meta.projection = get_string("projection");
    meta.width = get_int("width");
    meta.height = get_int("height");
    meta.capture_start = get_string("capture_start");
    meta.capture_end = get_string("capture_end");
    meta.created = get_string("created");
    meta.partial = get_bool("partial");
    meta.crc_recovered = get_bool("crc_recovered");
    meta.path = get_string("path");
    meta.ext = get_string("ext");
    const json::Value* geo = root.find("georeferencing");
    if (geo != nullptr && geo->is_object()) {
        const auto geo_double = [geo](const char* key) -> double {
            const json::Value* value = geo->find(key);
            return value != nullptr ? value->as_double() : 0.0;
        };
        meta.geo.valid = true;
        meta.geo.lon0_deg = geo_double("lon0_deg");
        meta.geo.altitude_m = geo_double("altitude_m");
        meta.geo.lon_min = geo_double("lon_min");
        meta.geo.lon_max = geo_double("lon_max");
        meta.geo.lat_min = geo_double("lat_min");
        meta.geo.lat_max = geo_double("lat_max");
    }
    return !meta.id.empty();
}

}  // namespace ed::output
