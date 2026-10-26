#include "core/config.h"

#include <set>

#include "core/default_config.h"
#include "core/logging.h"
#include "core/time_util.h"
#include "util/fs_util.h"
#include "util/string_util.h"

namespace ed {

namespace {

constexpr uint32_t kRtlsdrSampleRates[] = {250000,  1024000, 1536000, 2048000,
                                           2400000, 2560000, 3200000};

bool is_rtlsdr_rate(uint32_t rate) {
    for (uint32_t allowed : kRtlsdrSampleRates) {
        if (rate == allowed) {
            return true;
        }
    }
    return false;
}

}  // namespace

Config::Config() {
    root_ = json::Value::parse(default_config_json());
}

Config Config::defaults() {
    Config cfg;
    return cfg;
}

Config Config::load(const std::string& path, std::string* error) {
    Config cfg;
    cfg.source_path_ = path;

    if (!cfg.root_.is_object()) {
        if (error != nullptr) {
            *error = "embedded default config is not a JSON object";
        }
        return cfg;
    }

    if (fs::exists(path)) {
        bool ok = false;
        const std::string text = fs::read_file(path, &ok);
        if (!ok) {
            if (error != nullptr) {
                *error = "cannot read config file: " + path;
            }
            return cfg;
        }
        std::string parse_error;
        json::Value user = json::Value::parse(text, &parse_error);
        if (!parse_error.empty()) {
            if (error != nullptr) {
                *error = "failed to parse " + path + ": " + parse_error;
            }
            return cfg;
        }
        if (!user.is_object()) {
            if (error != nullptr) {
                *error = path + ": top-level value must be an object";
            }
            return cfg;
        }
        json::deep_merge(cfg.root_, user);
        cfg.loaded_from_file_ = true;
    }

    std::string migrate_error;
    if (!migrate(cfg.root_, &migrate_error)) {
        if (error != nullptr) {
            *error = migrate_error;
        }
        return cfg;
    }

    if (error != nullptr) {
        error->clear();
    }
    return cfg;
}

bool Config::migrate(json::Value& root, std::string* error) {
    const json::Value* version = root.find("version");
    if (version == nullptr || !version->is_number() || !version->is_integer()) {
        if (error != nullptr) {
            *error = "config.version is required and must be an integer";
        }
        return false;
    }
    const int64_t current = version->as_int();
    if (current > kVersion) {
        if (error != nullptr) {
            *error = "config.version " + std::to_string(current) +
                     " is newer than supported version " +
                     std::to_string(kVersion);
        }
        return false;
    }
    // Future migrations slot in here as `while (current < kVersion)` steps.
    if (current < kVersion) {
        root.set("version", json::Value(static_cast<int64_t>(kVersion)));
    }
    return true;
}

bool Config::validate(std::vector<std::string>* errors,
                      std::vector<std::string>* warnings) const {
    std::vector<std::string> errs;
    std::vector<std::string> warns;
    const auto err = [&errs](const std::string& m) { errs.push_back(m); };
    const auto warn = [&warns](const std::string& m) { warns.push_back(m); };

    if (!root_.is_object()) {
        err("root: config must be a JSON object");
        if (errors != nullptr) {
            *errors = errs;
        }
        if (warnings != nullptr) {
            *warnings = warns;
        }
        return false;
    }

    const json::Value* version = root_.find("version");
    if (version == nullptr || !version->is_number() ||
        !version->is_integer()) {
        err("version: required integer");
    } else if (version->as_int() != kVersion) {
        err("version: expected " + std::to_string(kVersion) + ", got " +
            std::to_string(version->as_int()));
    }

    std::string driver;
    const json::Value* driver_value = root_.find_path("sdr.driver");
    if (driver_value == nullptr || !driver_value->is_string()) {
        err("sdr.driver: required string");
    } else {
        driver = driver_value->as_string();
        if (driver != "rtlsdr" && driver != "airspy" && driver != "mock" &&
            driver != "file") {
            err("sdr.driver: must be 'rtlsdr', 'airspy', 'mock' or 'file'");
        }
    }

    const json::Value* hrit_rate = root_.find_path("hrit.sample_rate_hz");
    if (hrit_rate == nullptr || !hrit_rate->is_number()) {
        err("hrit.sample_rate_hz: required number");
    } else if (hrit_rate->as_double() < 6.0e6) {
        err("hrit.sample_rate_hz: must be >= 6000000");
    }
    // HRIT needs ~6 MSPS (beyond the RTL-SDR ceiling), so it requires an Airspy;
    // only enforced when the daemon is actually configured to run HRIT.
    const json::Value* run_mode_value = root_.find_path("run.mode");
    const std::string run_mode =
        (run_mode_value != nullptr && run_mode_value->is_string())
            ? run_mode_value->as_string()
            : "ggak";
    if (run_mode == "hrit") {
        if (driver == "rtlsdr") {
            err("sdr.driver: HRIT requires an Airspy (6 MSPS); RTL-SDR cannot "
                "receive HRIT");
        } else if (driver != "airspy" && driver != "mock" && driver != "file") {
            err("sdr.driver: HRIT requires an Airspy (6 MSPS)");
        }
    }

    const json::Value* lrit_rate = root_.find_path("lrit.sample_rate_hz");
    if (lrit_rate == nullptr || !lrit_rate->is_number()) {
        err("lrit.sample_rate_hz: required number");
    } else if (driver == "rtlsdr" &&
               !is_rtlsdr_rate(static_cast<uint32_t>(lrit_rate->as_int()))) {
        err("lrit.sample_rate_hz: " +
            std::to_string(lrit_rate->as_int()) +
            " is not a valid RTL-SDR sample rate");
    }

    const json::Value* cadu_bits = root_.find_path("decode.cadu_bits");
    if (cadu_bits == nullptr || !cadu_bits->is_number()) {
        err("decode.cadu_bits: required number");
    } else if (cadu_bits->as_int() != 1792 && cadu_bits->as_int() != 8192) {
        err("decode.cadu_bits: must be 1792 or 8192");
    }

    const json::Value* cal_enabled = root_.find_path("calibration.enabled");
    if (cal_enabled == nullptr || !cal_enabled->is_bool()) {
        err("calibration.enabled: required bool");
    }
    const json::Value* cal_table = root_.find_path("calibration.table");
    if (cal_table == nullptr || !cal_table->is_string()) {
        err("calibration.table: required string");
    } else if (!fs::exists(cal_table->as_string())) {
        warn("calibration.table: '" + cal_table->as_string() +
             "' does not exist (calibration disabled)");
    }

    const json::Value* channel_map = root_.find_path("image.channel_map");
    if (channel_map == nullptr || !channel_map->is_object()) {
        err("image.channel_map: required object");
    } else {
        for (const char* key : {"vis_r", "vis_g", "vis_b", "ir"}) {
            const json::Value* value = channel_map->find(key);
            if (value == nullptr || !value->is_number() ||
                !value->is_integer()) {
                err(std::string("image.channel_map.") + key +
                    ": required integer");
            } else if (value->as_int() < 0 || value->as_int() > 15) {
                err(std::string("image.channel_map.") + key +
                    ": must be in 0..15");
            }
        }
    }
    const json::Value* segment_height = root_.find_path("image.segment_height");
    if (segment_height == nullptr || !segment_height->is_number()) {
        err("image.segment_height: required number");
    } else if (segment_height->as_int() < 0 ||
               segment_height->as_int() > 4096) {
        err("image.segment_height: must be in 0..4096");
    }
    const json::Value* missing_ratio =
        root_.find_path("image.max_missing_segment_ratio");
    if (missing_ratio == nullptr || !missing_ratio->is_number()) {
        err("image.max_missing_segment_ratio: required number");
    } else if (missing_ratio->as_double() < 0.0 ||
               missing_ratio->as_double() > 1.0) {
        err("image.max_missing_segment_ratio: must be in 0..1");
    }

    for (const char* key :
         {"products.visual.r_min", "products.visual.r_max",
          "products.visual.g_min", "products.visual.g_max",
          "products.visual.b_min", "products.visual.b_max"}) {
        const json::Value* value = root_.find_path(key);
        if (value == nullptr || !value->is_number()) {
            err(std::string(key) + ": required number");
        }
    }
    for (const char* pair : {"r", "g", "b"}) {
        const std::string prefix = std::string("products.visual.") + pair;
        const json::Value* min_value = root_.find_path(prefix + "_min");
        const json::Value* max_value = root_.find_path(prefix + "_max");
        if (min_value != nullptr && max_value != nullptr &&
            min_value->is_number() && max_value->is_number() &&
            min_value->as_double() >= max_value->as_double()) {
            err(prefix + "_min: must be < " + pair + "_max");
        }
    }
    const json::Value* ir_min = root_.find_path("products.ir.min_k");
    const json::Value* ir_max = root_.find_path("products.ir.max_k");
    if (ir_min == nullptr || !ir_min->is_number()) {
        err("products.ir.min_k: required number");
    }
    if (ir_max == nullptr || !ir_max->is_number()) {
        err("products.ir.max_k: required number");
    }
    if (ir_min != nullptr && ir_max != nullptr && ir_min->is_number() &&
        ir_max->is_number() && ir_min->as_double() >= ir_max->as_double()) {
        err("products.ir.min_k: must be < max_k");
    }
    const json::Value* map_enabled =
        root_.find_path("products.ir_cloud.map_enabled");
    if (map_enabled == nullptr || !map_enabled->is_bool()) {
        err("products.ir_cloud.map_enabled: required bool");
    }
    const json::Value* map_path = root_.find_path("products.ir_cloud.map");
    if (map_path == nullptr || !map_path->is_string()) {
        err("products.ir_cloud.map: required string");
    } else if (!fs::exists(map_path->as_string())) {
        warn("products.ir_cloud.map: '" + map_path->as_string() +
             "' does not exist (map underlay disabled)");
    }
    const json::Value* queue_depth = root_.find_path("products.queue_depth");
    if (queue_depth == nullptr || !queue_depth->is_number()) {
        err("products.queue_depth: required number");
    } else if (queue_depth->as_int() < 1 || queue_depth->as_int() > 4096) {
        err("products.queue_depth: must be in 1..4096");
    }

    const json::Value* product_format = root_.find_path("products.format");
    if (product_format == nullptr || !product_format->is_string()) {
        err("products.format: required string");
    } else {
        const std::string format = product_format->as_string();
        if (format != "webp" && format != "png" && format != "ppm") {
            err("products.format: must be 'webp', 'png' or 'ppm'");
        }
    }
    const json::Value* webp_quality = root_.find_path("products.webp_quality");
    if (webp_quality == nullptr || !webp_quality->is_number()) {
        err("products.webp_quality: required number");
    } else if (webp_quality->as_int() < 1 || webp_quality->as_int() > 100) {
        err("products.webp_quality: must be in 1..100");
    }
    const json::Value* webp_method = root_.find_path("products.webp_method");
    if (webp_method == nullptr || !webp_method->is_number()) {
        err("products.webp_method: required number");
    } else if (webp_method->as_int() < 0 || webp_method->as_int() > 6) {
        err("products.webp_method: must be in 0..6");
    }
    const json::Value* products_days = root_.find_path("products.products_days");
    if (products_days == nullptr || !products_days->is_number()) {
        err("products.products_days: required number");
    } else if (products_days->as_int() < 1) {
        err("products.products_days: must be >= 1");
    }

    for (const char* key :
         {"frequencies.lo_hz", "frequencies.ggak_if_hz",
          "frequencies.lrit_if_hz", "frequencies.hrit_if_hz"}) {
        const json::Value* value = root_.find_path(key);
        if (value == nullptr || !value->is_number()) {
            err(std::string(key) + ": required number");
        } else if (value->as_int() < 0) {
            err(std::string(key) + ": must be >= 0");
        }
    }

    const json::Value* projection_type = root_.find_path("projection.type");
    if (projection_type == nullptr || !projection_type->is_string()) {
        err("projection.type: required string");
    } else {
        const std::string type = projection_type->as_string();
        if (type != "mercator" && type != "equirect" && type != "none") {
            err("projection.type: must be 'mercator', 'equirect' or 'none'");
        }
    }
    for (const char* key : {"projection.output_width",
                            "projection.output_height"}) {
        const json::Value* value = root_.find_path(key);
        if (value == nullptr || !value->is_number()) {
            err(std::string(key) + ": required number");
        } else if (value->as_int() <= 0 || value->as_int() > 4096) {
            err(std::string(key) + ": must be in 1..4096");
        }
    }

    const json::Value* schedule_enabled = root_.find_path("schedule.enabled");
    if (schedule_enabled == nullptr || !schedule_enabled->is_bool()) {
        err("schedule.enabled: required bool");
    }
    const json::Value* schedule_timezone = root_.find_path("schedule.timezone");
    if (schedule_timezone == nullptr || !schedule_timezone->is_string() ||
        schedule_timezone->as_string().empty()) {
        err("schedule.timezone: required non-empty string");
    } else if (schedule_timezone->as_string() != "UTC") {
        warn("schedule.timezone: '" + schedule_timezone->as_string() +
             "' is not UTC; only UTC and fixed UTC offsets are supported");
    }
    const json::Value* schedule_default =
        root_.find_path("schedule.default_mode");
    if (schedule_default == nullptr || !schedule_default->is_string()) {
        err("schedule.default_mode: required string");
    } else {
        const std::string mode = schedule_default->as_string();
        if (mode != "ggak" && mode != "lrit" && mode != "hrit") {
            err("schedule.default_mode: must be 'ggak', 'lrit' or 'hrit'");
        }
    }

    const json::Value* entries = root_.find_path("schedule.entries");
    if (entries != nullptr && !entries->is_array()) {
        err("schedule.entries: must be an array");
    } else if (entries != nullptr) {
        std::set<std::string> names;
        for (std::size_t i = 0; i < entries->size(); ++i) {
            const json::Value& entry = (*entries)[i];
            const std::string prefix =
                "schedule.entries[" + std::to_string(i) + "]";
            if (!entry.is_object()) {
                err(prefix + ": must be an object");
                continue;
            }
            const json::Value* name = entry.find("name");
            if (name == nullptr || !name->is_string() ||
                name->as_string().empty()) {
                err(prefix + ".name: required non-empty string");
            } else if (!names.insert(name->as_string()).second) {
                err(prefix + ".name: duplicate name '" + name->as_string() +
                    "'");
            }

            const json::Value* start = entry.find("start");
            const json::Value* end = entry.find("end");
            bool start_ok = false;
            bool end_ok = false;
            const int start_min =
                (start != nullptr && start->is_string())
                    ? time::parse_hhmm(start->as_string(), &start_ok)
                    : -1;
            const int end_min = (end != nullptr && end->is_string())
                                    ? time::parse_hhmm(end->as_string(), &end_ok)
                                    : -1;
            if (!start_ok) {
                err(prefix + ".start: expected HH:MM");
            }
            if (!end_ok) {
                err(prefix + ".end: expected HH:MM");
            }
            if (start_ok && end_ok && start_min == end_min) {
                err(prefix + ".end: must differ from start");
            }

            const json::Value* mode = entry.find("mode");
            if (mode == nullptr || !mode->is_string() ||
                (mode->as_string() != "lrit" &&
                 mode->as_string() != "hrit")) {
                err(prefix + ".mode: must be 'lrit' or 'hrit'");
            }

            const json::Value* priority = entry.find("priority");
            if (priority != nullptr &&
                (!priority->is_number() || !priority->is_integer())) {
                err(prefix + ".priority: must be an integer");
            }
            const json::Value* frequency = entry.find("frequency_hz");
            if (frequency != nullptr &&
                (!frequency->is_number() || frequency->as_int() < 0)) {
                err(prefix + ".frequency_hz: must be >= 0");
            }
            const json::Value* symbol_rate = entry.find("symbol_rate");
            if (symbol_rate != nullptr &&
                (!symbol_rate->is_number() || symbol_rate->as_double() < 0.0)) {
                err(prefix + ".symbol_rate: must be >= 0");
            }
            const json::Value* sample_rate = entry.find("sample_rate_hz");
            if (sample_rate != nullptr &&
                (!sample_rate->is_number() || sample_rate->as_int() < 0)) {
                err(prefix + ".sample_rate_hz: must be >= 0");
            }
        }
    }

    const json::Value* tick_seconds = root_.find_path("scheduler.tick_seconds");
    if (tick_seconds == nullptr || !tick_seconds->is_number()) {
        err("scheduler.tick_seconds: required number");
    } else if (tick_seconds->as_int() < 1 || tick_seconds->as_int() > 60) {
        err("scheduler.tick_seconds: must be in 1..60");
    }
    const json::Value* janitor_seconds =
        root_.find_path("scheduler.janitor_seconds");
    if (janitor_seconds == nullptr || !janitor_seconds->is_number()) {
        err("scheduler.janitor_seconds: required number");
    } else if (janitor_seconds->as_int() < 10 ||
               janitor_seconds->as_int() > 3600) {
        err("scheduler.janitor_seconds: must be in 10..3600");
    }
    const json::Value* failed_cadu =
        root_.find_path("storage.retention.failed_cadu_hours");
    if (failed_cadu == nullptr || !failed_cadu->is_number()) {
        err("storage.retention.failed_cadu_hours: required number");
    } else if (failed_cadu->as_int() < 1) {
        err("storage.retention.failed_cadu_hours: must be >= 1");
    }

    const json::Value* storage_root = root_.find_path("storage.root");
    if (storage_root == nullptr || !storage_root->is_string()) {
        err("storage.root: required string");
    } else {
        const std::string path = storage_root->as_string();
        if (fs::exists(path)) {
            if (!fs::is_directory(path)) {
                err("storage.root: not a directory: " + path);
            } else if (!fs::is_writable(path)) {
                err("storage.root: not writable: " + path);
            }
        } else {
            // The service may create this later, so a missing root is only a warning.
            warn("storage.root: '" + path +
                 "' does not exist yet (will be created)");
        }
    }

    const json::Value* log_level = root_.find_path("logging.level");
    LogLevel parsed_level = LogLevel::Info;
    if (log_level == nullptr || !log_level->is_string()) {
        err("logging.level: required string");
    } else if (!parse_log_level(log_level->as_string(), parsed_level)) {
        err("logging.level: must be one of trace, debug, info, warn, error, "
            "off");
    }

    const json::Value* port = root_.find_path("web.port");
    if (port == nullptr || !port->is_number()) {
        err("web.port: required number");
    } else if (port->as_int() < 1 || port->as_int() > 65535) {
        err("web.port: must be in 1..65535");
    }
    const json::Value* static_root = root_.find_path("web.static_root");
    if (static_root == nullptr || !static_root->is_string()) {
        err("web.static_root: required string");
    }

    const json::Value* base_url = root_.find_path("api.base_url");
    if (base_url == nullptr || !base_url->is_string()) {
        err("api.base_url: required string");
    }
    const json::Value* webhooks = root_.find_path("api.webhooks");
    if (webhooks == nullptr || !webhooks->is_array()) {
        err("api.webhooks: required array");
    } else {
        for (std::size_t i = 0; i < webhooks->size(); ++i) {
            const json::Value& hook = (*webhooks)[i];
            const std::string prefix =
                "api.webhooks[" + std::to_string(i) + "]";
            if (!hook.is_object()) {
                err(prefix + ": must be an object");
                continue;
            }
            const json::Value* url = hook.find("url");
            if (url == nullptr || !url->is_string() ||
                url->as_string().empty()) {
                err(prefix + ".url: required non-empty string");
            }
            const json::Value* secret = hook.find("secret");
            if (secret != nullptr && !secret->is_string()) {
                err(prefix + ".secret: must be a string");
            }
            const json::Value* events = hook.find("events");
            if (events != nullptr) {
                if (!events->is_array()) {
                    err(prefix + ".events: must be an array");
                } else {
                    for (std::size_t j = 0; j < events->size(); ++j) {
                        const json::Value& event = (*events)[j];
                        if (!event.is_string()) {
                            err(prefix + ".events[" + std::to_string(j) +
                                "]: must be a string");
                        } else if (event.as_string() != "product.created" &&
                                   event.as_string() != "test") {
                            err(prefix + ".events[" + std::to_string(j) +
                                "]: unknown event '" + event.as_string() + "'");
                        }
                    }
                }
            }
        }
    }

    if (errors != nullptr) {
        *errors = errs;
    }
    if (warnings != nullptr) {
        *warnings = warns;
    }
    return errs.empty();
}

bool Config::save(const std::string& path, std::string* error) const {
    const std::string parent = fs::dirname(path);
    if (!parent.empty() && parent != "." && !fs::mkdir_p(parent)) {
        if (error != nullptr) {
            *error = "cannot create directory: " + parent;
        }
        return false;
    }
    if (!fs::atomic_write(path, dump(2) + "\n")) {
        if (error != nullptr) {
            *error = "cannot write config file: " + path;
        }
        return false;
    }
    return true;
}

std::string Config::dump(int indent) const {
    return root_.dump(indent);
}

std::string Config::get_string(const std::string& path,
                               const std::string& def) const {
    const json::Value* value = root_.find_path(path);
    return value != nullptr ? value->as_string(def) : def;
}

int64_t Config::get_int(const std::string& path, int64_t def) const {
    const json::Value* value = root_.find_path(path);
    return value != nullptr ? value->as_int(def) : def;
}

double Config::get_double(const std::string& path, double def) const {
    const json::Value* value = root_.find_path(path);
    return value != nullptr ? value->as_double(def) : def;
}

bool Config::get_bool(const std::string& path, bool def) const {
    const json::Value* value = root_.find_path(path);
    return value != nullptr ? value->as_bool(def) : def;
}

}  // namespace ed
