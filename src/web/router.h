// ElektroDump — tiny method+path router (PLAN.md D9).
// Endpoints are (method, path, handler) triples; paths match exactly after
// normalising a trailing slash, keeping the router trivial.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ed::web {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> params;

    std::string param(const std::string& key, const std::string& def = "") const;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    static HttpResponse json(const std::string& body, int status = 200);
    static HttpResponse text(const std::string& body, int status = 200);
    static HttpResponse html(const std::string& body, int status = 200);
    static HttpResponse error(int status, const std::string& message);
};

using Handler = std::function<HttpResponse(const HttpRequest&)>;

class Router {
public:
    void add(const std::string& method, const std::string& path,
             Handler handler);
    void get(const std::string& path, Handler handler);
    void post(const std::string& path, Handler handler);
    void put(const std::string& path, Handler handler);

    // Prefix route: matches any path starting with `prefix`; exact routes are
    // checked first, so it acts as a catch-all for sub-paths.
    void get_prefix(const std::string& prefix, Handler handler);

    HttpResponse handle(const HttpRequest& req) const;
    bool has(const std::string& method, const std::string& path) const;
    std::size_t size() const { return routes_.size(); }

private:
    struct Route {
        std::string method;
        std::string path;
        Handler handler;
        bool prefix = false;
    };
    std::vector<Route> routes_;
};

// Normalise a request path: strip a trailing slash (except for "/").
std::string normalize_path(const std::string& path);

// Parse "a=1&b=2" into a map, URL-decoding keys and values.
std::map<std::string, std::string> parse_query(const std::string& query);

std::string url_decode(const std::string& s);

}  // namespace ed::web
