#include "scheduler/schedule_model.h"

#include <cstdio>
#include <set>

#include "core/time_util.h"
#include "util/string_util.h"

namespace ed::scheduler {

namespace {

std::string hhmm(int minute_of_day) {
    int m = minute_of_day % 1440;
    if (m < 0) {
        m += 1440;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
    return buf;
}

// Parse a start/end field: "HH:MM" string or integer minutes-of-day; false and
// a field error on failure.
bool parse_time_field(const json::Value& entry, const char* key,
                      const std::string& prefix, int& out,
                      std::vector<FieldError>* errors) {
    const json::Value* value = entry.find(key);
    if (value == nullptr) {
        if (errors != nullptr) {
            errors->push_back({prefix + "." + key, "expected HH:MM"});
        }
        return false;
    }
    if (value->is_string()) {
        bool ok = false;
        const int minutes = time::parse_hhmm(value->as_string(), &ok);
        if (!ok) {
            if (errors != nullptr) {
                errors->push_back({prefix + "." + key, "expected HH:MM"});
            }
            return false;
        }
        out = minutes;
        return true;
    }
    if (value->is_number() && value->is_integer()) {
        out = static_cast<int>(value->as_int());
        return true;
    }
    if (errors != nullptr) {
        errors->push_back({prefix + "." + key, "expected HH:MM"});
    }
    return false;
}

}  // namespace

Schedule parse_schedule(const json::Value& root, std::vector<FieldError>* errors) {
    Schedule s;
    if (!root.is_object()) {
        return s;
    }

    const json::Value* enabled = root.find("enabled");
    if (enabled != nullptr && enabled->is_bool()) {
        s.enabled = enabled->as_bool();
    }
    const json::Value* timezone = root.find("timezone");
    if (timezone != nullptr && timezone->is_string() &&
        !timezone->as_string().empty()) {
        s.timezone = timezone->as_string();
    }
    const json::Value* default_mode = root.find("default_mode");
    if (default_mode != nullptr && default_mode->is_string() &&
        !default_mode->as_string().empty()) {
        s.default_mode = str::to_lower(default_mode->as_string());
    }

    const json::Value* entries = root.find("entries");
    if (entries == nullptr || !entries->is_array()) {
        return s;
    }
    for (std::size_t i = 0; i < entries->size(); ++i) {
        const json::Value& e = (*entries)[i];
        const std::string prefix =
            "schedule.entries[" + std::to_string(i) + "]";
        if (!e.is_object()) {
            if (errors != nullptr) {
                errors->push_back({prefix, "must be an object"});
            }
            continue;
        }
        ScheduleEntry entry;
        const json::Value* name = e.find("name");
        if (name != nullptr && name->is_string()) {
            entry.name = name->as_string();
        }
        parse_time_field(e, "start", prefix, entry.start_minute_of_day, errors);
        parse_time_field(e, "end", prefix, entry.end_minute_of_day, errors);

        const json::Value* mode = e.find("mode");
        if (mode != nullptr && mode->is_string()) {
            entry.mode = str::to_lower(mode->as_string());
        }
        const json::Value* priority = e.find("priority");
        if (priority != nullptr && priority->is_number()) {
            entry.priority = static_cast<int>(priority->as_int());
        }
        const json::Value* frequency = e.find("frequency_hz");
        if (frequency != nullptr && frequency->is_number()) {
            const int64_t value = frequency->as_int();
            entry.frequency_hz = value > 0 ? static_cast<uint64_t>(value) : 0;
        }
        const json::Value* symbol_rate = e.find("symbol_rate");
        if (symbol_rate != nullptr && symbol_rate->is_number()) {
            entry.symbol_rate = symbol_rate->as_double();
        }
        const json::Value* sample_rate = e.find("sample_rate_hz");
        if (sample_rate != nullptr && sample_rate->is_number()) {
            const int64_t value = sample_rate->as_int();
            entry.sample_rate_hz = value > 0 ? static_cast<uint32_t>(value) : 0;
        }
        const json::Value* constellation = e.find("constellation");
        if (constellation != nullptr && constellation->is_string()) {
            entry.constellation = str::to_lower(constellation->as_string());
        }
        s.entries.push_back(entry);
    }
    return s;
}

json::Value schedule_to_json(const Schedule& s) {
    json::Value root;
    root.set("enabled", json::Value(s.enabled));
    root.set("timezone", json::Value(s.timezone));
    root.set("default_mode", json::Value(s.default_mode));

    json::Value::Array arr;
    arr.reserve(s.entries.size());
    for (const ScheduleEntry& e : s.entries) {
        json::Value entry;
        entry.set("name", json::Value(e.name));
        entry.set("start", json::Value(hhmm(e.start_minute_of_day)));
        entry.set("end", json::Value(hhmm(e.end_minute_of_day)));
        entry.set("mode", json::Value(e.mode));
        entry.set("priority", json::Value(static_cast<int64_t>(e.priority)));
        entry.set("frequency_hz",
                  json::Value(static_cast<int64_t>(e.frequency_hz)));
        entry.set("symbol_rate", json::Value(e.symbol_rate));
        entry.set("sample_rate_hz",
                  json::Value(static_cast<int64_t>(e.sample_rate_hz)));
        if (!e.constellation.empty()) {
            entry.set("constellation", json::Value(e.constellation));
        }
        arr.push_back(std::move(entry));
    }
    root.set("entries", json::Value(std::move(arr)));
    return root;
}

bool validate_schedule(const Schedule& s, std::vector<FieldError>* errors) {
    std::vector<FieldError> errs;
    if (s.timezone.empty()) {
        errs.push_back({"schedule.timezone", "must be non-empty"});
    }
    if (s.default_mode != "ggak" && s.default_mode != "lrit" &&
        s.default_mode != "hrit") {
        errs.push_back(
            {"schedule.default_mode", "must be 'ggak', 'lrit' or 'hrit'"});
    }

    std::set<std::string> names;
    for (std::size_t i = 0; i < s.entries.size(); ++i) {
        const ScheduleEntry& e = s.entries[i];
        const std::string prefix =
            "schedule.entries[" + std::to_string(i) + "]";
        if (e.name.empty()) {
            errs.push_back({prefix + ".name", "required non-empty string"});
        } else if (!names.insert(e.name).second) {
            errs.push_back({prefix + ".name",
                            "duplicate name '" + e.name + "'"});
        }
        if (e.start_minute_of_day < 0 || e.start_minute_of_day > 1439) {
            errs.push_back({prefix + ".start", "must be in 00:00..23:59"});
        }
        if (e.end_minute_of_day < 0 || e.end_minute_of_day > 1439) {
            errs.push_back({prefix + ".end", "must be in 00:00..23:59"});
        }
        if (e.start_minute_of_day == e.end_minute_of_day) {
            errs.push_back({prefix + ".end", "must differ from start"});
        }
        if (e.mode != "lrit" && e.mode != "hrit") {
            errs.push_back({prefix + ".mode", "must be 'lrit' or 'hrit'"});
        }
        if (e.symbol_rate < 0.0) {
            errs.push_back({prefix + ".symbol_rate", "must be >= 0"});
        }
    }

    if (errors != nullptr) {
        errors->insert(errors->end(), errs.begin(), errs.end());
    }
    return errs.empty();
}

}  // namespace ed::scheduler
