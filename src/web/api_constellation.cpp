// ElektroDump — constellation endpoint.
// `/api/v1/constellation` returns the demodulated symbol cloud (I/Q pairs) so the
// web UI can show what the demodulator is actually recovering. Without this the
// only feedback is an SNR number, which stays plausible even when the demod is
// tracking something that is not the intended signal.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "core/time_util.h"
#include "util/json_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

// Symbols are scaled by the symbol RMS so the cloud always fills the plot.
json::Value points_json(const std::vector<std::complex<float>>& symbols,
                        double& rms_out) {
    double power = 0.0;
    for (const std::complex<float>& s : symbols) {
        power += static_cast<double>(s.real()) * s.real() +
                 static_cast<double>(s.imag()) * s.imag();
    }
    const double rms = symbols.empty()
                           ? 0.0
                           : std::sqrt(power / static_cast<double>(symbols.size()));
    rms_out = rms;
    const double scale = rms > 1e-9 ? 1.0 / rms : 1.0;

    json::Value::Array arr;
    arr.reserve(symbols.size());
    for (const std::complex<float>& s : symbols) {
        json::Value::Array pair;
        pair.push_back(json::Value(static_cast<double>(s.real()) * scale));
        pair.push_back(json::Value(static_cast<double>(s.imag()) * scale));
        arr.push_back(json::Value(std::move(pair)));
    }
    return json::Value(std::move(arr));
}

}  // namespace

void register_constellation_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/constellation", [&ctx](const HttpRequest& req) {
        if (!ctx.constellation) {
            return HttpResponse::error(503, "no_pipeline");
        }
        std::vector<std::complex<float>> symbols;
        ctx.constellation(symbols);
        if (symbols.empty()) {
            return HttpResponse::error(503, "no_symbols");
        }

        // Optional cap so a caller cannot ask for an unbounded payload.
        std::size_t limit = 8192;
        const std::string requested = req.param("limit");
        if (!requested.empty()) {
            try {
                const long value = std::stol(requested);
                if (value > 0 && value <= 8192) {
                    limit = static_cast<std::size_t>(value);
                }
            } catch (...) {
            }
        }
        if (symbols.size() > limit) {
            symbols.erase(symbols.begin(),
                          symbols.end() - static_cast<std::ptrdiff_t>(limit));
        }

        double rms = 0.0;
        json::Value out;
        out.set("points", points_json(symbols, rms));
        out.set("count", json::Value(static_cast<int64_t>(symbols.size())));
        out.set("rms", json::Value(rms));
        out.set("mode", json::Value(ctx.mode));
        out.set("timestamp", json::Value(time::iso8601_now(false)));
        return HttpResponse::json(out.dump(0));
    });
}

}  // namespace ed::web
