// ElektroDump — GGAK science REST endpoints (spec §4.4, PLAN.md D9/C.4).
// GET /api/v1/ggak (recent samples), /ggak/plots, /ggak/csv, and
// /ggak/plots/{channel}.svg (server-rendered). These read the in-memory
// GgakScienceStore fed by the live pipeline; with no SKL frames seen they return
// well-formed empty results rather than fabricating values.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/time_util.h"
#include "ggak/ggak_science.h"
#include "util/json_util.h"
#include "util/string_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

constexpr std::size_t kDefaultLimit = 360;

std::string utc_from_unix(double unix_time) {
    if (!(unix_time > 0.0)) {
        return "";
    }
    const std::time_t t = static_cast<std::time_t>(unix_time);
    return time::format_utc(t, "%Y-%m-%dT%H:%M:%SZ");
}

std::string format_double(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return std::string(buffer);
}

std::size_t parse_limit(const HttpRequest& req) {
    const std::string text = req.param("limit");
    if (text.empty()) {
        return kDefaultLimit;
    }
    try {
        const long value = std::stol(text);
        if (value > 0 && value <= 100000) {
            return static_cast<std::size_t>(value);
        }
    } catch (...) {
    }
    return kDefaultLimit;
}

bool parse_channel(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    try {
        const long value = std::stol(text);
        if (value >= 0 && value < ggak::kGgakSklChannels) {
            out = static_cast<int>(value);
            return true;
        }
    } catch (...) {
    }
    return false;
}

json::Value sample_json(const ggak::GgakSample& sample) {
    json::Value item;
    item.set("utc", json::Value(utc_from_unix(sample.time)));
    item.set("value", json::Value(sample.value));
    item.set("crc_ok", json::Value(sample.crc_ok));
    return item;
}

json::Value stats_json(const ggak::GgakScienceStore& store) {
    const ggak::GgakScienceStore::Stats stats = store.stats();
    json::Value out;
    out.set("frames", json::Value(static_cast<int64_t>(stats.frames)));
    out.set("skl_frames", json::Value(static_cast<int64_t>(stats.skl_frames)));
    out.set("records", json::Value(static_cast<int64_t>(stats.records)));
    return out;
}

// Tiny dependency-free line chart; empty series render a "no data" notice.
std::string build_svg(const std::string& title, const std::string& unit,
                      const std::vector<ggak::GgakSample>& samples) {
    constexpr int kWidth = 800;
    constexpr int kHeight = 260;
    constexpr int kMargin = 40;

    std::string svg;
    svg.reserve(4096);
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" "
           "height=\"260\" viewBox=\"0 0 800 260\">";
    svg += "<rect width=\"800\" height=\"260\" fill=\"#0b0f14\"/>";
    svg += "<text x=\"12\" y=\"22\" fill=\"#d7e0ea\" font-family=\"monospace\" "
           "font-size=\"14\">";
    svg += title + " (" + unit + ")";
    svg += "</text>";

    if (samples.empty()) {
        svg += "<text x=\"400\" y=\"130\" fill=\"#7f93a8\" "
               "font-family=\"monospace\" font-size=\"14\" "
               "text-anchor=\"middle\">no data yet</text>";
        svg += "</svg>";
        return svg;
    }

    double min_value = samples[0].value;
    double max_value = samples[0].value;
    for (const ggak::GgakSample& sample : samples) {
        if (sample.value < min_value) {
            min_value = sample.value;
        }
        if (sample.value > max_value) {
            max_value = sample.value;
        }
    }
    const double span = (max_value - min_value) > 0.0 ? (max_value - min_value)
                                                      : 1.0;

    const double plot_w = kWidth - 2.0 * kMargin;
    const double plot_h = kHeight - 2.0 * kMargin;
    const double denom =
        samples.size() > 1 ? static_cast<double>(samples.size() - 1) : 1.0;
    std::string points;
    points.reserve(samples.size() * 16);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double x =
            kMargin + plot_w * (static_cast<double>(i) / denom);
        const double y =
            kMargin + plot_h * (1.0 - (samples[i].value - min_value) / span);
        if (i != 0) {
            points.push_back(' ');
        }
        points += format_double(x);
        points.push_back(',');
        points += format_double(y);
    }

    svg += "<polyline fill=\"none\" stroke=\"#4fd1c5\" stroke-width=\"1.5\" "
           "points=\"";
    svg += points;
    svg += "\"/>";
    svg += "<text x=\"" + std::to_string(kMargin) + "\" y=\"" +
           std::to_string(kHeight - 8) +
           "\" fill=\"#7f93a8\" font-family=\"monospace\" font-size=\"12\">"
           "min " +
           format_double(min_value) + " / max " + format_double(max_value) +
           "</text>";
    svg += "<text x=\"" + std::to_string(kWidth - kMargin) + "\" y=\"" +
           std::to_string(kHeight - 8) +
           "\" fill=\"#7f93a8\" font-family=\"monospace\" font-size=\"12\" "
           "text-anchor=\"end\">" +
           std::to_string(samples.size()) + " samples</text>";
    svg += "</svg>";
    return svg;
}

}  // namespace

void register_ggak_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/ggak", [&ctx](const HttpRequest& req) {
        json::Value root;
        if (ctx.ggak == nullptr) {
            root.set("available", json::Value(false));
            root.set("reason", json::Value("no_ggak_store"));
            root.set("channels", json::Value(json::Value::Array{}));
            return HttpResponse::json(root.dump(0));
        }

        const ggak::GgakScienceStore& store = *ctx.ggak;
        const std::size_t limit = parse_limit(req);
        int channel = -1;
        const bool single = parse_channel(req.param("channel"), channel);

        if (single) {
            std::vector<ggak::GgakSample> samples;
            store.series(channel, limit, samples);
            const std::vector<ggak::GgakChannelInfo> info = store.channels();
            json::Value::Array arr;
            arr.reserve(samples.size());
            for (const ggak::GgakSample& sample : samples) {
                arr.push_back(sample_json(sample));
            }
            root.set("available", json::Value(store.has_data()));
            root.set("channel", json::Value(static_cast<int64_t>(channel)));
            root.set("name", json::Value(
                                 channel < static_cast<int>(info.size())
                                     ? info[static_cast<std::size_t>(channel)]
                                           .name
                                     : std::string("channel")));
            root.set("unit", json::Value("counts"));
            root.set("samples", json::Value(std::move(arr)));
            root.set("count", json::Value(static_cast<int64_t>(samples.size())));
            root.set("timestamp", json::Value(time::iso8601_now(false)));
            return HttpResponse::json(root.dump(0));
        }

        const std::vector<ggak::GgakChannelInfo> info = store.channels();
        const std::vector<ggak::GgakScienceStore::ChannelLatest> latest =
            store.latest();
        json::Value::Array arr;
        arr.reserve(info.size());
        for (const ggak::GgakChannelInfo& channel_info : info) {
            json::Value item;
            item.set("id", json::Value(static_cast<int64_t>(channel_info.id)));
            item.set("name", json::Value(channel_info.name));
            item.set("unit", json::Value(channel_info.unit));
            item.set("present", json::Value(channel_info.present));
            const std::size_t index =
                static_cast<std::size_t>(channel_info.id);
            const ggak::GgakScienceStore::ChannelLatest& last = latest[index];
            item.set("has_value", json::Value(last.has_value));
            item.set("latest", json::Value(last.value));
            item.set("time", json::Value(utc_from_unix(last.time)));
            item.set("count",
                     json::Value(static_cast<int64_t>(
                         store.sample_count(channel_info.id))));
            arr.push_back(std::move(item));
        }
        root.set("available", json::Value(store.has_data()));
        root.set("channels", json::Value(std::move(arr)));
        root.set("stats", stats_json(store));
        root.set("timestamp", json::Value(time::iso8601_now(false)));
        return HttpResponse::json(root.dump(0));
    });

    router.get("/api/v1/ggak/plots", [&ctx](const HttpRequest&) {
        json::Value root;
        json::Value::Array arr;
        bool available = false;
        if (ctx.ggak != nullptr) {
            const ggak::GgakScienceStore& store = *ctx.ggak;
            available = store.has_data();
            for (const ggak::GgakChannelInfo& channel : store.channels()) {
                if (!channel.present) {
                    continue;
                }
                json::Value item;
                item.set("channel", json::Value(static_cast<int64_t>(channel.id)));
                item.set("name", json::Value(channel.name));
                item.set("unit", json::Value(channel.unit));
                item.set("points",
                         json::Value(static_cast<int64_t>(
                             store.sample_count(channel.id))));
                item.set("svg_url",
                         json::Value("/api/v1/ggak/plots/" +
                                     std::to_string(channel.id) + ".svg"));
                item.set("csv_url",
                         json::Value("/api/v1/ggak/csv?channel=" +
                                     std::to_string(channel.id)));
                arr.push_back(std::move(item));
            }
        }
        root.set("available", json::Value(available));
        root.set("plots", json::Value(std::move(arr)));
        return HttpResponse::json(root.dump(0));
    });

    router.get("/api/v1/ggak/csv", [&ctx](const HttpRequest& req) {
        int channel = 0;
        if (!parse_channel(req.param("channel"), channel)) {
            channel = 0;
        }
        std::string body = "utc,channel,value,crc_ok\n";
        if (ctx.ggak != nullptr) {
            std::vector<ggak::GgakSample> samples;
            ctx.ggak->series(channel, parse_limit(req), samples);
            for (const ggak::GgakSample& sample : samples) {
                body += utc_from_unix(sample.time);
                body.push_back(',');
                body += std::to_string(channel);
                body.push_back(',');
                body += format_double(sample.value);
                body.push_back(',');
                body += sample.crc_ok ? "1" : "0";
                body.push_back('\n');
            }
        }
        HttpResponse response;
        response.status = 200;
        response.content_type = "text/csv; charset=utf-8";
        response.body = body;
        return response;
    });

    router.get_prefix("/api/v1/ggak/plots/", [&ctx](const HttpRequest& req) {
        const std::string prefix = "/api/v1/ggak/plots/";
        std::string rest = req.path;
        if (rest.compare(0, prefix.size(), prefix) == 0) {
            rest = rest.substr(prefix.size());
        }
        if (str::ends_with(rest, ".svg")) {
            rest = rest.substr(0, rest.size() - 4);
        }
        int channel = -1;
        if (!parse_channel(rest, channel)) {
            return HttpResponse::error(404, "unknown_channel");
        }
        std::vector<ggak::GgakSample> samples;
        std::string name = "SKL-" + std::to_string(channel);
        if (ctx.ggak != nullptr) {
            ctx.ggak->series(channel, 100000, samples);
            const std::vector<ggak::GgakChannelInfo> info = ctx.ggak->channels();
            if (channel < static_cast<int>(info.size())) {
                name = info[static_cast<std::size_t>(channel)].name;
            }
        }
        HttpResponse response;
        response.status = 200;
        response.content_type = "image/svg+xml";
        response.body = build_svg(name, "counts", samples);
        return response;
    });
}

}  // namespace ed::web
