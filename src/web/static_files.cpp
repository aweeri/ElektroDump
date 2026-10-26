// ElektroDump — static web UI serving (spec §4.3, milestone M9).
// The UI under `web/` is embedded at build time (build/gen/web_assets.inc) and
// served from kWebAssets[]; a real `web.static_root` directory is preferred for
// hot reload. Extensionless page paths map to `<path>.html`, else exact match.
#include <cstddef>
#include <string>

#include "util/fs_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

struct WebAsset {
    const char* path;
    const char* mime;
    const char* data;
    std::size_t size;
};

#include "web_assets.inc"

std::string mime_for_path(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }
    const std::string ext = path.substr(dot);
    if (ext == ".html" || ext == ".htm") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".css") {
        return "text/css; charset=utf-8";
    }
    if (ext == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (ext == ".json" || ext == ".map") {
        return "application/json; charset=utf-8";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".ico") {
        return "image/x-icon";
    }
    if (ext == ".webmanifest") {
        return "application/manifest+json; charset=utf-8";
    }
    if (ext == ".txt") {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

HttpResponse error_not_found() { return HttpResponse::error(404, "not_found"); }

HttpResponse serve_asset(const WebContext& ctx, const HttpRequest& req) {
    std::string rel = req.path;
    if (!rel.empty() && rel[0] == '/') {
        rel.erase(0, 1);
    }
    if (rel.empty()) {
        rel = "index.html";
    }
    // Reject path traversal outright.
    if (rel.find("..") != std::string::npos || rel.find('\0') != std::string::npos) {
        return error_not_found();
    }

    // Extensionless page paths map to the matching .html file.
    const std::size_t slash = rel.find_last_of('/');
    const std::string last =
        slash == std::string::npos ? rel : rel.substr(slash + 1);
    std::string resolved = rel;
    if (last.find('.') == std::string::npos) {
        resolved = rel + ".html";
    }

    // Disk override (development hot reload).
    if (ctx.config != nullptr) {
        const std::string root =
            ctx.config->get_string("web.static_root", "web");
        if (!root.empty() && fs::is_directory(root)) {
            const std::string disk_path = fs::join(root, resolved);
            if (fs::is_regular_file(disk_path)) {
                bool ok = false;
                const std::string body = fs::read_file(disk_path, &ok);
                if (ok) {
                    HttpResponse response;
                    response.status = 200;
                    response.content_type = mime_for_path(resolved);
                    response.body = body;
                    return response;
                }
            }
        }
    }

    for (std::size_t i = 0; i < kWebAssetCount; ++i) {
        if (resolved == kWebAssets[i].path) {
            HttpResponse response;
            response.status = 200;
            response.content_type = kWebAssets[i].mime;
            response.body.assign(kWebAssets[i].data, kWebAssets[i].size);
            return response;
        }
    }
    return error_not_found();
}

}  // namespace

void register_static_routes(Router& router, const WebContext& ctx) {
    // Catch-all registered last: exact API routes are matched first.
    router.get_prefix("/",
                      [&ctx](const HttpRequest& req) { return serve_asset(ctx, req); });
}

}  // namespace ed::web
