// ElektroDump — FFT / waterfall endpoints (PLAN.md D8/D9).
// `/api/v1/fft` returns the latest spectrum row; `/api/v1/waterfall` the last N rows.
#include <cstdint>
#include <string>
#include <vector>

#include "core/time_util.h"
#include "util/json_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

json::Value bins_json(const std::vector<float>& bins) {
    json::Value::Array arr;
    arr.reserve(bins.size());
    for (float value : bins) {
        arr.push_back(json::Value(static_cast<double>(value)));
    }
    return json::Value(std::move(arr));
}

json::Value frame_json(const dsp::SpectrumFrame& frame) {
    json::Value out;
    out.set("bins", bins_json(frame.bins_db));
    out.set("center_hz", json::Value(static_cast<int64_t>(frame.center_hz)));
    out.set("span_hz", json::Value(static_cast<int64_t>(frame.span_hz)));
    out.set("timestamp", json::Value(time::iso8601_now(false)));
    return out;
}

}  // namespace

void register_fft_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/fft", [&ctx](const HttpRequest&) {
        if (ctx.waterfall == nullptr) {
            return HttpResponse::error(503, "no_waterfall");
        }
        dsp::SpectrumFrame frame;
        if (!ctx.waterfall->latest(frame)) {
            return HttpResponse::error(503, "no_spectrum");
        }
        return HttpResponse::json(frame_json(frame).dump(0));
    });

    router.get("/api/v1/waterfall", [&ctx](const HttpRequest& req) {
        if (ctx.waterfall == nullptr) {
            return HttpResponse::error(503, "no_waterfall");
        }
        std::size_t rows = 32;
        const std::string requested = req.param("rows");
        if (!requested.empty()) {
            try {
                const long value = std::stol(requested);
                if (value > 0 && value <= 256) {
                    rows = static_cast<std::size_t>(value);
                }
            } catch (...) {
            }
        }
        const std::vector<dsp::SpectrumFrame> frames =
            ctx.waterfall->rows(rows);
        json::Value::Array arr;
        arr.reserve(frames.size());
        for (const dsp::SpectrumFrame& frame : frames) {
            arr.push_back(frame_json(frame));
        }
        json::Value out;
        out.set("rows", json::Value(std::move(arr)));
        out.set("count", json::Value(static_cast<int64_t>(frames.size())));
        out.set("fft_size",
                json::Value(static_cast<int64_t>(ctx.waterfall->fft_size())));
        return HttpResponse::json(out.dump(0));
    });
}

}  // namespace ed::web
