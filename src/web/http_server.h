// ElektroDump — minimal HTTP/1.1 transport (PLAN.md D8/D9).
// Thin wrapper over libmicrohttpd when available; otherwise the server reports
// itself unavailable and the daemon keeps running. The router is transport
// agnostic, so swapping the transport touches only this file.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "web/router.h"

namespace ed::web {

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Start listening on `bind`:`port`. `router` must outlive the server.
    bool start(const std::string& bind, uint16_t port, Router* router);
    void stop();

    bool running() const { return running_; }
    uint16_t port() const { return port_; }

    // True when an HTTP transport was compiled into this binary.
    static bool available();

    // The router this server dispatches to (used by the transport callback).
    Router* router() const { return router_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Router* router_ = nullptr;
    uint16_t port_ = 0;
    bool running_ = false;
};

}  // namespace ed::web
