// ElektroDump - history endpoints.
//
// GET /api/v1/history/snr?limit=N  one-second SNR samples, oldest first, so the
// dashboard chart has a curve the moment the page opens instead of starting
// empty and growing. `avg5_db` is the mean of the newest five samples.
#include <cstddef>
#include <string>
#include <vector>

#include "util/json_util.h"
#include "web/snr_history.h"
#include "web/web_server.h"

namespace ed::web {

void register_history_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/history/snr", [&ctx](const HttpRequest& req) {
        if (ctx.snr_history == nullptr) {
            return HttpResponse::error(503, "no_history");
        }
        std::size_t limit = 300;
        const std::string requested = req.param("limit");
        if (!requested.empty()) {
            try {
                const long value = std::stol(requested);
                if (value > 0 && value <= 1800) {
                    limit = static_cast<std::size_t>(value);
                }
            } catch (...) {
            }
        }

        const std::vector<SnrPoint> points = ctx.snr_history->latest(limit);
        json::Value::Array samples;
        samples.reserve(points.size());
        for (const SnrPoint& point : points) {
            samples.push_back(json::Value(static_cast<double>(point.snr_db)));
        }

        json::Value out;
        out.set("step_seconds", json::Value(1.0));
        out.set("count", json::Value(static_cast<double>(points.size())));
        out.set("avg5_db",
                json::Value(static_cast<double>(ctx.snr_history->average(5))));
        out.set("mode", json::Value(points.empty()
                                        ? 0
                                        : static_cast<int>(points.back().mode)));
        out.set("samples", json::Value(std::move(samples)));
        return HttpResponse::json(out.dump(0));
    });
}

}  // namespace ed::web
