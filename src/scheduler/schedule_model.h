// ElektroDump — capture schedule model (spec §3.3, milestone M8).
// A list of time-of-day entries plus timezone and fallback mode, parsed from and
// serialised to the config `schedule` object, shared by the CLI editor, REST API
// and runtime Scheduler.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/json_util.h"

namespace ed::scheduler {

struct ScheduleEntry {
    std::string name;
    int start_minute_of_day = 0;  // 0..1439, in the schedule timezone
    int end_minute_of_day = 0;
    std::string mode;              // "lrit" | "hrit"
    uint64_t frequency_hz = 0;     // 0 = use frequencies.<mode>_hz
    std::string constellation;     // "bpsk" | "oqpsk" (default from mode)
    double symbol_rate = 0.0;      // 0 = use <mode>.symbol_rate
    uint32_t sample_rate_hz = 0;   // 0 = use <mode>.sample_rate_hz
    int priority = 0;              // higher wins on overlap
};

struct Schedule {
    bool enabled = true;
    std::string timezone = "UTC";
    std::string default_mode = "ggak";
    std::vector<ScheduleEntry> entries;
};

struct FieldError {
    std::string path;
    std::string message;
};

// Parse the `schedule` object (not the whole config); accepts "HH:MM" strings
// and integer minutes-of-day. Field-level problems are appended to `errors`.
Schedule parse_schedule(const json::Value& root, std::vector<FieldError>* errors);

// Serialise to the config form ("start"/"end" as "HH:MM" so it re-validates).
json::Value schedule_to_json(const Schedule& s);

// Validate the model; field-level problems are appended to `errors`.
bool validate_schedule(const Schedule& s, std::vector<FieldError>* errors);

}  // namespace ed::scheduler
