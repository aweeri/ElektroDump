#include "web/http_server.h"

#include <cstring>

#include "core/logging.h"

#ifdef ED_HAVE_MHD
#include <arpa/inet.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ed::web {

#ifdef ED_HAVE_MHD

#if defined(MHD_VERSION) && MHD_VERSION >= 0x00097002
using MhdResult = enum MHD_Result;
#else
using MhdResult = int;
#endif

namespace {

struct ConnContext {
    std::string body;
    bool created = false;
};

MhdResult access_handler(void* cls, MHD_Connection* connection,
                         const char* url, const char* method,
                         const char* /*version*/, const char* upload_data,
                         std::size_t* upload_data_size, void** con_cls) {
    auto* server = static_cast<HttpServer*>(cls);
    auto* router = server->router();

    if (*con_cls == nullptr) {
        auto* ctx = new ConnContext();
        ctx->created = true;
        *con_cls = ctx;
        return MHD_YES;
    }
    auto* ctx = static_cast<ConnContext*>(*con_cls);

    if (*upload_data_size != 0) {
        ctx->body.append(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }

    HttpRequest req;
    req.method = method != nullptr ? method : "GET";
    const std::string raw_url = url != nullptr ? url : "/";
    const std::size_t qpos = raw_url.find('?');
    if (qpos == std::string::npos) {
        req.path = raw_url;
    } else {
        req.path = raw_url.substr(0, qpos);
        req.query = raw_url.substr(qpos + 1);
        req.params = parse_query(req.query);
    }
    req.body = ctx->body;

    const HttpResponse resp = router->handle(req);

    MHD_Response* mhd_response = MHD_create_response_from_buffer(
        resp.body.size(), const_cast<char*>(resp.body.data()),
        MHD_RESPMEM_MUST_COPY);
    if (mhd_response == nullptr) {
        return MHD_NO;
    }
    MHD_add_response_header(mhd_response, "Content-Type",
                            resp.content_type.c_str());
    MHD_add_response_header(mhd_response, "Cache-Control", "no-cache");
    for (const auto& header : resp.headers) {
        MHD_add_response_header(mhd_response, header.first.c_str(),
                                header.second.c_str());
    }
    const MhdResult queued =
        MHD_queue_response(connection, static_cast<unsigned int>(resp.status),
                           mhd_response);
    MHD_destroy_response(mhd_response);
    return queued;
}

void request_completed(void* /*cls*/, MHD_Connection* /*connection*/,
                       void** con_cls, MHD_RequestTerminationCode /*code*/) {
    if (con_cls != nullptr && *con_cls != nullptr) {
        delete static_cast<ConnContext*>(*con_cls);
        *con_cls = nullptr;
    }
}

}  // namespace

#endif  // ED_HAVE_MHD

struct HttpServer::Impl {
#ifdef ED_HAVE_MHD
    MHD_Daemon* daemon = nullptr;
#endif
};

HttpServer::HttpServer() : impl_(std::make_unique<Impl>()) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::available() {
#ifdef ED_HAVE_MHD
    return true;
#else
    return false;
#endif
}

bool HttpServer::start(const std::string& bind, uint16_t port, Router* router) {
    if (running_) {
        return true;
    }
    router_ = router;
    port_ = port;
#ifdef ED_HAVE_MHD
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind.empty() || bind == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind.c_str(), &addr.sin_addr) != 1) {
        ED_LOG(Error, "web") << "invalid web.bind address: " << bind;
        return false;
    }

    impl_->daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD, port, nullptr, nullptr,
        &access_handler, this, MHD_OPTION_SOCK_ADDR, &addr,
        MHD_OPTION_NOTIFY_COMPLETED, &request_completed, nullptr,
        MHD_OPTION_END);
    if (impl_->daemon == nullptr) {
        ED_LOG(Error, "web") << "failed to start HTTP server on " << bind << ":"
                             << port;
        return false;
    }
    running_ = true;
    ED_LOG(Info, "web") << "HTTP server listening on " << bind << ":" << port;
    return true;
#else
    (void)bind;
    ED_LOG(Warn, "web")
        << "HTTP server not compiled in (libmicrohttpd missing); "
           "status API unavailable";
    return false;
#endif
}

void HttpServer::stop() {
#ifdef ED_HAVE_MHD
    if (impl_ != nullptr && impl_->daemon != nullptr) {
        MHD_stop_daemon(impl_->daemon);
        impl_->daemon = nullptr;
    }
#endif
    running_ = false;
}

}  // namespace ed::web
