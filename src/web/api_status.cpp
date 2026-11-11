// ElektroDump — status / signal / health / config endpoints (PLAN.md D9).
// They expose the daemon's live state as JSON for the web UI.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

#include <sys/statvfs.h>

#include "core/app.h"
#include "core/logging.h"
#include "core/time_util.h"
#include "output/product_store.h"
#include "scheduler/scheduler.h"
#include "util/json_util.h"
#include "web/web_server.h"

namespace ed::web {

namespace {

int64_t uptime_seconds(const WebContext& ctx) {
    if (ctx.start_time == std::chrono::steady_clock::time_point{}) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - ctx.start_time)
        .count();
}

struct CpuSample {
    uint64_t total = 0;
    uint64_t idle = 0;
};

CpuSample read_cpu() {
    CpuSample s;
    std::ifstream in("/proc/stat");
    std::string label;
    if (!(in >> label) || label != "cpu") {
        return s;
    }
    uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0,
             softirq = 0, steal = 0;
    in >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    s.idle = idle + iowait;
    s.total = user + nice + system + idle + iowait + irq + softirq + steal;
    return s;
}

double cpu_percent() {
    static std::mutex mutex;
    static CpuSample prev;
    static bool have_prev = false;
    std::lock_guard<std::mutex> lock(mutex);
    const CpuSample now = read_cpu();
    double percent = 0.0;
    if (have_prev && now.total > prev.total) {
        const uint64_t dt = now.total - prev.total;
        const uint64_t di = now.idle - prev.idle;
        percent = 100.0 * static_cast<double>(dt - di) / static_cast<double>(dt);
    }
    prev = now;
    have_prev = true;
    return percent;
}

void read_memory(uint64_t& used_mb, uint64_t& total_mb) {
    std::ifstream in("/proc/meminfo");
    std::string key;
    uint64_t value = 0;
    std::string unit;
    uint64_t total_kb = 0;
    uint64_t avail_kb = 0;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") {
            total_kb = value;
        } else if (key == "MemAvailable:") {
            avail_kb = value;
        }
        if (total_kb != 0 && avail_kb != 0) {
            break;
        }
    }
    total_mb = total_kb / 1024;
    used_mb = (total_kb > avail_kb ? total_kb - avail_kb : 0) / 1024;
}

double read_temperature_c() {
    std::ifstream in("/sys/class/thermal/thermal_zone0/temp");
    long milli = 0;
    if (in >> milli) {
        return static_cast<double>(milli) / 1000.0;
    }
    return 0.0;
}

double read_load1() {
    std::ifstream in("/proc/loadavg");
    double load = 0.0;
    in >> load;
    return load;
}

void read_disk(const std::string& path, uint64_t& free_mb, uint64_t& total_mb) {
    struct statvfs vfs;
    if (statvfs(path.c_str(), &vfs) != 0) {
        return;
    }
    const uint64_t block = vfs.f_frsize != 0 ? vfs.f_frsize : vfs.f_bsize;
    total_mb = (static_cast<uint64_t>(vfs.f_blocks) * block) / (1024 * 1024);
    free_mb = (static_cast<uint64_t>(vfs.f_bavail) * block) / (1024 * 1024);
}

json::Value sdr_json(const WebContext& ctx) {
    json::Value sdr;
    if (ctx.sdr != nullptr) {
        const sdr::SdrInfo info = ctx.sdr->info();
        const sdr::SdrStats stats = ctx.sdr->stats();
        sdr.set("available", json::Value(true));
        sdr.set("driver", json::Value(info.driver));
        sdr.set("name", json::Value(info.name));
        sdr.set("serial", json::Value(info.serial));
        sdr.set("center_freq_hz",
                json::Value(static_cast<int64_t>(ctx.sdr->center_freq_hz())));
        sdr.set("sample_rate_hz",
                json::Value(static_cast<int64_t>(ctx.sdr->sample_rate_hz())));
        sdr.set("gain_tenth_db",
                json::Value(static_cast<int64_t>(ctx.sdr->gain_tenth_db())));
        sdr.set("bias_tee", json::Value(ctx.sdr->bias_tee()));
        int64_t overflows = static_cast<int64_t>(stats.overflows);
        if (ctx.metrics != nullptr) {
            overflows =
                static_cast<int64_t>(ctx.metrics->snapshot().sdr_overflows);
        }
        sdr.set("overflows", json::Value(overflows));
        sdr.set("samples", json::Value(static_cast<int64_t>(stats.samples)));
    } else {
        sdr.set("available", json::Value(false));
        sdr.set("error", json::Value(ctx.sdr_error));
    }
    return sdr;
}

// Prefer the App-provided snapshot (the scheduler may swap the pipeline),
// falling back to the cached pointer.
PipelineStats pipeline_stats(const WebContext& ctx, bool& present) {
    if (ctx.pipeline_snapshot) {
        const PipelineSnapshot snap = ctx.pipeline_snapshot();
        present = snap.present;
        return snap.stats;
    }
    if (ctx.pipeline != nullptr) {
        present = true;
        return ctx.pipeline->stats();
    }
    present = false;
    return PipelineStats{};
}

json::Value pipeline_json(const WebContext& ctx) {
    json::Value pipeline;
    bool present = false;
    const PipelineStats ps = pipeline_stats(ctx, present);
    pipeline.set("state", json::Value(ps.running ? "running" : "idle"));
    pipeline.set("locked", json::Value(ps.locked));
    pipeline.set("demod_locked", json::Value(ps.demod_locked));
    pipeline.set("ber", json::Value(ps.ber));
    pipeline.set("snr_db", json::Value(ps.snr_db));
    pipeline.set("cadus_total",
                 json::Value(static_cast<int64_t>(ps.cadus_total)));
    pipeline.set("cadus_bad", json::Value(static_cast<int64_t>(ps.cadus_bad)));
    pipeline.set("files_total",
                 json::Value(static_cast<int64_t>(ps.files_total)));
    pipeline.set("files_recovered",
                 json::Value(static_cast<int64_t>(ps.files_recovered)));
    if (ctx.metrics != nullptr) {
        const MetricsSnapshot m = ctx.metrics->snapshot();
        pipeline.set("iq_samples", json::Value(static_cast<int64_t>(m.iq_samples)));
        if (!present) {
            pipeline.set("snr_db", json::Value(m.snr_db));
            pipeline.set("ber", json::Value(m.ber));
            pipeline.set("locked", json::Value(m.locked));
        pipeline.set("demod_locked", json::Value(m.locked));
        }
    }
    return pipeline;
}

json::Value product_meta_json(const output::ProductMeta& meta) {
    json::Value root;
    root.set("id", json::Value(meta.id));
    root.set("satellite", json::Value(meta.satellite));
    root.set("product", json::Value(meta.product));
    root.set("projection", json::Value(meta.projection));
    root.set("width", json::Value(static_cast<int64_t>(meta.width)));
    root.set("height", json::Value(static_cast<int64_t>(meta.height)));
    root.set("created", json::Value(meta.created));
    root.set("partial", json::Value(meta.partial));
    root.set("path", json::Value(meta.path));
    root.set("ext", json::Value(meta.ext));
    return root;
}

void redact_secrets(json::Value& value) {
    if (value.is_object()) {
        for (auto& entry : value.object()) {
            if (entry.first == "secret" || entry.first == "password" ||
                entry.first == "token") {
                entry.second = json::Value("***");
            } else {
                redact_secrets(entry.second);
            }
        }
    } else if (value.is_array()) {
        for (auto& item : value.array()) {
            redact_secrets(item);
        }
    }
}

}  // namespace

void register_status_api(Router& router, const WebContext& ctx) {
    router.get("/api/v1/status", [&ctx](const HttpRequest&) {
        json::Value root;
        root.set("version", json::Value(kVersion));
        root.set("uptime_seconds",
                 json::Value(static_cast<int64_t>(uptime_seconds(ctx))));
        root.set("satellite", json::Value(ctx.satellite));
        std::string mode = ctx.mode;
        if (ctx.mode_code != nullptr) {
            mode = pipeline_mode_name(static_cast<PipelineMode>(
                ctx.mode_code->load(std::memory_order_relaxed)));
        }
        root.set("mode", json::Value(mode));
        root.set("sdr", sdr_json(ctx));
        root.set("pipeline", pipeline_json(ctx));

        json::Value schedule;
        if (ctx.scheduler != nullptr) {
            const scheduler::ScheduleState st = ctx.scheduler->state();
            schedule.set("enabled", json::Value(st.enabled));
            schedule.set("active_entry",
                         st.active_entry.empty()
                             ? json::Value(nullptr)
                             : json::Value(st.active_entry));
            schedule.set("next_entry", st.next_entry.empty()
                                           ? json::Value(nullptr)
                                           : json::Value(st.next_entry));
            schedule.set("next_transition_utc",
                         st.next_transition_utc.empty()
                             ? json::Value(nullptr)
                             : json::Value(st.next_transition_utc));
            schedule.set("seconds_to_next",
                         json::Value(static_cast<int64_t>(st.seconds_to_next)));
        } else {
            schedule.set("enabled",
                         json::Value(ctx.config != nullptr
                                         ? ctx.config->get_bool(
                                               "schedule.enabled", true)
                                         : false));
            schedule.set("active_entry", json::Value(nullptr));
            schedule.set("next_entry", json::Value(nullptr));
            schedule.set("next_transition_utc", json::Value(nullptr));
            schedule.set("seconds_to_next", json::Value(static_cast<int64_t>(0)));
        }
        root.set("schedule", schedule);

        json::Value waterfall;
        if (ctx.waterfall != nullptr) {
            waterfall.set("fft_size",
                          json::Value(static_cast<int64_t>(ctx.waterfall->fft_size())));
            waterfall.set("rate_hz",
                          json::Value(static_cast<int64_t>(ctx.waterfall->rate_hz())));
            waterfall.set("frames",
                          json::Value(static_cast<int64_t>(ctx.waterfall->frames())));
            waterfall.set("running", json::Value(ctx.waterfall->running()));
        }
        root.set("waterfall", waterfall);

        json::Value latest_products;
        if (ctx.products != nullptr) {
            for (const char* product :
                 {"visual", "ir", "ir_cloud", "ir_cloud_map"}) {
                output::ProductMeta meta;
                if (ctx.products->latest(product, meta)) {
                    latest_products.set(product, product_meta_json(meta));
                }
            }
        }
        root.set("latest_products", latest_products);
        root.set("timestamp", json::Value(time::iso8601_now(false)));
        return HttpResponse::json(root.dump(0));
    });

    router.get("/api/v1/signal", [&ctx](const HttpRequest&) {
        json::Value root;
        double snr = 0.0;
        double ber = 0.0;
        bool locked = false;
        bool present = false;
        const PipelineStats ps = pipeline_stats(ctx, present);
        if (present) {
            snr = ps.snr_db;
            ber = ps.ber;
            locked = ps.locked;
        } else if (ctx.metrics != nullptr) {
            const MetricsSnapshot m = ctx.metrics->snapshot();
            snr = m.snr_db;
            ber = m.ber;
            locked = m.locked;
        }
        root.set("snr_db", json::Value(snr));
        root.set("locked", json::Value(locked));
        root.set("ber", json::Value(ber));
        if (ctx.sdr != nullptr) {
            root.set("center_freq_hz",
                     json::Value(static_cast<int64_t>(ctx.sdr->center_freq_hz())));
            root.set("sample_rate_hz",
                     json::Value(static_cast<int64_t>(ctx.sdr->sample_rate_hz())));
            root.set("gain_tenth_db",
                     json::Value(static_cast<int64_t>(ctx.sdr->gain_tenth_db())));
            root.set("bias_tee", json::Value(ctx.sdr->bias_tee()));
            int64_t overflows =
                static_cast<int64_t>(ctx.sdr->stats().overflows);
            if (ctx.metrics != nullptr) {
                overflows =
                    static_cast<int64_t>(ctx.metrics->snapshot().sdr_overflows);
            }
            root.set("overflows", json::Value(overflows));
        } else {
            root.set("center_freq_hz", json::Value(static_cast<int64_t>(0)));
            root.set("sample_rate_hz", json::Value(static_cast<int64_t>(0)));
            root.set("gain_tenth_db", json::Value(static_cast<int64_t>(0)));
            root.set("bias_tee", json::Value(false));
            root.set("overflows", json::Value(static_cast<int64_t>(0)));
        }
        root.set("timestamp", json::Value(time::iso8601_now(false)));
        return HttpResponse::json(root.dump(0));
    });

    router.get("/api/v1/health", [&ctx](const HttpRequest&) {
        uint64_t mem_used = 0;
        uint64_t mem_total = 0;
        read_memory(mem_used, mem_total);
        uint64_t disk_free = 0;
        uint64_t disk_total = 0;
        const std::string root =
            ctx.config != nullptr ? ctx.config->get_string("storage.root", ".")
                                  : ".";
        read_disk(root, disk_free, disk_total);

        json::Value out;
        out.set("cpu_percent", json::Value(cpu_percent()));
        out.set("memory_used_mb", json::Value(static_cast<int64_t>(mem_used)));
        out.set("memory_total_mb", json::Value(static_cast<int64_t>(mem_total)));
        out.set("temperature_c", json::Value(read_temperature_c()));
        out.set("disk_free_mb", json::Value(static_cast<int64_t>(disk_free)));
        out.set("disk_total_mb", json::Value(static_cast<int64_t>(disk_total)));
        out.set("uptime_seconds",
                json::Value(static_cast<int64_t>(uptime_seconds(ctx))));
        out.set("load_1m", json::Value(read_load1()));
        return HttpResponse::json(out.dump(0));
    });

    router.get("/api/v1/config", [&ctx](const HttpRequest&) {
        if (ctx.config == nullptr) {
            return HttpResponse::error(503, "no_config");
        }
        json::Value copy = ctx.config->root();
        redact_secrets(copy);
        return HttpResponse::json(copy.dump(2));
    });
}

}  // namespace ed::web
