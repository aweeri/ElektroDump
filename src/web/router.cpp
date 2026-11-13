#include "web/router.h"

#include <cctype>

namespace ed::web {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex_value(s[i + 1]);
            const int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& query) {
    std::map<std::string, std::string> params;
    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::string pair =
            query.substr(start, amp == std::string::npos ? std::string::npos
                                                         : amp - start);
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            if (eq == std::string::npos) {
                params[url_decode(pair)] = "";
            } else {
                params[url_decode(pair.substr(0, eq))] =
                    url_decode(pair.substr(eq + 1));
            }
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
    return params;
}

std::string normalize_path(const std::string& path) {
    if (path.empty()) {
        return "/";
    }
    if (path.size() > 1 && path.back() == '/') {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

std::string HttpRequest::param(const std::string& key,
                               const std::string& def) const {
    const auto it = params.find(key);
    return it != params.end() ? it->second : def;
}

HttpResponse HttpResponse::json(const std::string& body, int status) {
    HttpResponse r;
    r.status = status;
    r.content_type = "application/json";
    r.body = body;
    return r;
}

HttpResponse HttpResponse::text(const std::string& body, int status) {
    HttpResponse r;
    r.status = status;
    r.content_type = "text/plain; charset=utf-8";
    r.body = body;
    return r;
}

HttpResponse HttpResponse::html(const std::string& body, int status) {
    HttpResponse r;
    r.status = status;
    r.content_type = "text/html; charset=utf-8";
    r.body = body;
    return r;
}

HttpResponse HttpResponse::error(int status, const std::string& message) {
    return json("{\"error\":\"" + message + "\"}", status);
}

void Router::add(const std::string& method, const std::string& path,
                 Handler handler) {
    routes_.push_back({method, normalize_path(path), std::move(handler), false});
}

void Router::get(const std::string& path, Handler handler) {
    add("GET", path, std::move(handler));
}

void Router::post(const std::string& path, Handler handler) {
    add("POST", path, std::move(handler));
}

void Router::put(const std::string& path, Handler handler) {
    add("PUT", path, std::move(handler));
}

void Router::get_prefix(const std::string& prefix, Handler handler) {
    routes_.push_back({"GET", prefix, std::move(handler), true});
}

bool Router::has(const std::string& method, const std::string& path) const {
    const std::string norm = normalize_path(path);
    for (const Route& route : routes_) {
        if (route.method == method && route.path == norm) {
            return true;
        }
    }
    return false;
}

HttpResponse Router::handle(const HttpRequest& req) const {
    if (read_only_ && req.method != "GET" && req.method != "HEAD") {
        return HttpResponse::error(405, "read_only");
    }
    const std::string norm = normalize_path(req.path);
    bool path_matched = false;
    for (const Route& route : routes_) {
        if (route.prefix) {
            if (route.method == req.method &&
                norm.compare(0, route.path.size(), route.path) == 0) {
                return route.handler(req);
            }
            continue;
        }
        if (route.path != norm) {
            continue;
        }
        path_matched = true;
        if (route.method == req.method) {
            return route.handler(req);
        }
    }
    if (path_matched) {
        return HttpResponse::error(405, "method_not_allowed");
    }
    return HttpResponse::error(404, "not_found");
}

}  // namespace ed::web
