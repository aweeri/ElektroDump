// ElektroDump — product REST endpoints (spec §2.6).
// GET /api/v1/products (list), /products/latest (newest image),
// /products/{id} (metadata JSON), /products/{id}/image (image bytes).
#include <cstdint>
#include <string>
#include <vector>

#include "output/product_store.h"
#include "util/fs_util.h"
#include "util/json_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

json::Value meta_json(const output::ProductMeta& meta) {
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
    return root;
}

std::string content_type_for(const std::string& ext) {
    if (ext == "webp") {
        return "image/webp";
    }
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "ppm") {
        return "image/x-portable-pixmap";
    }
    return "application/octet-stream";
}

HttpResponse image_response(const output::ProductStore& store,
                            const output::ProductMeta& meta) {
    bool ok = false;
    const std::string bytes = fs::read_file(store.image_path(meta), &ok);
    if (!ok) {
        return HttpResponse::error(404, "no_image");
    }
    HttpResponse response;
    response.status = 200;
    response.content_type = content_type_for(meta.ext);
    response.body = bytes;
    response.headers.emplace_back("ETag", meta.id);
    response.headers.emplace_back("Cache-Control", "no-cache");
    return response;
}

}  // namespace

void register_products_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/products", [&ctx](const HttpRequest& req) {
        if (ctx.products == nullptr) {
            return HttpResponse::error(503, "no_product_store");
        }
        const std::string type = req.param("type");
        const std::string date = req.param("date");
        int limit = 50;
        const std::string limit_text = req.param("limit");
        if (!limit_text.empty()) {
            try {
                const long value = std::stol(limit_text);
                if (value > 0 && value <= 1000) {
                    limit = static_cast<int>(value);
                }
            } catch (...) {
            }
        }
        std::vector<output::ProductMeta> products;
        ctx.products->list(type, date, limit, products);
        json::Value::Array arr;
        arr.reserve(products.size());
        for (const output::ProductMeta& meta : products) {
            arr.push_back(meta_json(meta));
        }
        json::Value root;
        root.set("products", json::Value(std::move(arr)));
        root.set("total", json::Value(static_cast<int64_t>(products.size())));
        return HttpResponse::json(root.dump(0));
    });

    router.get("/api/v1/products/latest", [&ctx](const HttpRequest& req) {
        if (ctx.products == nullptr) {
            return HttpResponse::error(503, "no_product_store");
        }
        const std::string product = req.param("product", "visual");
        output::ProductMeta meta;
        if (!ctx.products->latest(product, meta)) {
            return HttpResponse::error(404, "no_product");
        }
        return image_response(*ctx.products, meta);
    });

    router.get_prefix("/api/v1/products/", [&ctx](const HttpRequest& req) {
        if (ctx.products == nullptr) {
            return HttpResponse::error(503, "no_product_store");
        }
        const std::string prefix = "/api/v1/products/";
        std::string rest = req.path;
        if (rest.compare(0, prefix.size(), prefix) == 0) {
            rest = rest.substr(prefix.size());
        }
        const bool want_image =
            rest.size() > 6 && rest.compare(rest.size() - 6, 6, "/image") == 0;
        if (want_image) {
            rest = rest.substr(0, rest.size() - 6);
        }
        output::ProductMeta meta;
        if (!ctx.products->get(rest, meta)) {
            return HttpResponse::error(404, "no_product");
        }
        if (want_image) {
            return image_response(*ctx.products, meta);
        }
        return HttpResponse::json(meta_json(meta).dump(0));
    });
}

}  // namespace ed::web
