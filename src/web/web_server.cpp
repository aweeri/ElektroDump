#include "web/web_server.h"

#include "core/logging.h"

namespace ed::web {

WebServer::~WebServer() { stop(); }

bool WebServer::start(const Config& cfg, const WebContext& ctx) {
    ctx_ = ctx;
    if (ctx_.config == nullptr) {
        ctx_.config = &cfg;
    }

    register_status_api(router_, ctx_);
    register_history_api(router_, ctx_);
    register_fft_api(router_, ctx_);
    register_constellation_api(router_, ctx_);
    register_products_api(router_, ctx_);
    register_schedule_api(router_, ctx_);
    register_ggak_api(router_, ctx_);
    register_webhook_api(router_, ctx_);
    // The static catch-all is registered last so the API routes win.
    register_static_routes(router_, ctx_);

    const std::string bind = cfg.get_string("web.bind", "0.0.0.0");
    const uint16_t port =
        static_cast<uint16_t>(cfg.get_int("web.port", 8080));
    return server_.start(bind, port, &router_);
}

void WebServer::stop() { server_.stop(); }

}  // namespace ed::web
