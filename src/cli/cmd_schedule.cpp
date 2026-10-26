// ElektroDump — `schedule` CLI editor (spec §3.6, milestone M8).
// Loads the user config, mutates `schedule.entries`, validates and persists it
// atomically. Exit codes: 0 success, 2 usage error, 1 validation failure.
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "cli/cli.h"
#include "core/config.h"
#include "core/time_util.h"
#include "scheduler/schedule_model.h"
#include "util/json_util.h"
#include "util/string_util.h"

namespace ed::cli {

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

void print_field_errors(const std::vector<scheduler::FieldError>& errors) {
    for (const scheduler::FieldError& e : errors) {
        std::cerr << "error: " << e.path << ": " << e.message << "\n";
    }
}

int save_schedule(Config& config, const scheduler::Schedule& sched,
                  const std::string& path) {
    config.root().set("schedule", scheduler::schedule_to_json(sched));
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (!config.validate(&errors, &warnings)) {
        for (const std::string& e : errors) {
            std::cerr << "error: " << e << "\n";
        }
        return 1;
    }
    std::string error;
    if (!config.save(path, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    return 0;
}

int cmd_add(Config& config, scheduler::Schedule& sched, const Args& args,
            const std::string& path) {
    std::string name;
    std::string start;
    std::string end;
    std::string mode;
    int priority = 0;
    uint64_t frequency = 0;
    double symbol_rate = 0.0;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&](const char* flag) -> std::string {
            if (i + 1 >= args.size()) {
                std::cerr << "error: missing value for " << flag << "\n";
                return {};
            }
            return args[++i];
        };
        if (arg == "--name") {
            name = value("--name");
        } else if (arg == "--start") {
            start = value("--start");
        } else if (arg == "--end") {
            end = value("--end");
        } else if (arg == "--mode") {
            mode = value("--mode");
        } else if (arg == "--priority") {
            const std::string v = value("--priority");
            try {
                priority = std::stoi(v);
            } catch (...) {
                std::cerr << "error: invalid --priority: " << v << "\n";
                return 2;
            }
        } else if (arg == "--frequency") {
            const std::string v = value("--frequency");
            try {
                frequency = std::stoull(v);
            } catch (...) {
                std::cerr << "error: invalid --frequency: " << v << "\n";
                return 2;
            }
        } else if (arg == "--symbol-rate") {
            const std::string v = value("--symbol-rate");
            try {
                symbol_rate = std::stod(v);
            } catch (...) {
                std::cerr << "error: invalid --symbol-rate: " << v << "\n";
                return 2;
            }
        } else {
            std::cerr << "error: unknown option: " << arg << "\n";
            return 2;
        }
    }

    if (name.empty() || start.empty() || end.empty() || mode.empty()) {
        std::cerr << "error: add requires --name, --start, --end and --mode\n";
        return 2;
    }

    bool ok = false;
    scheduler::ScheduleEntry entry;
    entry.name = name;
    entry.start_minute_of_day = time::parse_hhmm(start, &ok);
    if (!ok) {
        std::cerr << "error: --start: expected HH:MM\n";
        return 2;
    }
    entry.end_minute_of_day = time::parse_hhmm(end, &ok);
    if (!ok) {
        std::cerr << "error: --end: expected HH:MM\n";
        return 2;
    }
    entry.mode = str::to_lower(mode);
    entry.priority = priority;
    entry.frequency_hz = frequency;
    entry.symbol_rate = symbol_rate;

    // Replace an existing entry with the same name.
    for (auto it = sched.entries.begin(); it != sched.entries.end(); ++it) {
        if (it->name == name) {
            sched.entries.erase(it);
            break;
        }
    }
    sched.entries.push_back(entry);

    std::vector<scheduler::FieldError> errors;
    if (!scheduler::validate_schedule(sched, &errors)) {
        print_field_errors(errors);
        return 1;
    }
    return save_schedule(config, sched, path);
}

}  // namespace

int cmd_schedule(const Options& opts, const Args& args) {
    if (args.empty()) {
        std::cerr << "error: schedule requires a subcommand "
                     "(list|add|remove|enable|disable)\n";
        return 2;
    }
    const std::string& sub = args[0];
    if (sub != "list" && sub != "add" && sub != "remove" &&
        sub != "enable" && sub != "disable") {
        std::cerr << "error: unknown schedule subcommand: " << sub << "\n";
        return 2;
    }

    std::string load_error;
    Config config = Config::load(opts.config_path, &load_error);
    if (!load_error.empty()) {
        std::cerr << "error: " << load_error << "\n";
        return 1;
    }

    const json::Value* sched_value = config.root().find_path("schedule");
    std::vector<scheduler::FieldError> parse_errors;
    scheduler::Schedule sched = scheduler::parse_schedule(
        sched_value != nullptr ? *sched_value : json::Value(), &parse_errors);

    if (sub == "list") {
        std::cout << "schedule: " << (sched.enabled ? "enabled" : "disabled")
                  << "  timezone: " << sched.timezone
                  << "  default: " << sched.default_mode << "\n";
        if (sched.entries.empty()) {
            std::cout << "  (no entries)\n";
            return 0;
        }
        std::cout << "  name                 start  end    mode  prio  "
                     "freq_hz     sym_rate  sample_hz\n";
        for (const scheduler::ScheduleEntry& e : sched.entries) {
            std::printf("  %-20s %s  %s  %-4s  %4d  %-10llu  %-9.0f  %u\n",
                        e.name.c_str(), hhmm(e.start_minute_of_day).c_str(),
                        hhmm(e.end_minute_of_day).c_str(), e.mode.c_str(),
                        e.priority,
                        static_cast<unsigned long long>(e.frequency_hz),
                        e.symbol_rate, e.sample_rate_hz);
        }
        return 0;
    }

    if (sub == "add") {
        return cmd_add(config, sched, args, opts.config_path);
    }

    if (sub == "remove") {
        if (args.size() < 2) {
            std::cerr << "error: remove requires <name>\n";
            return 2;
        }
        const std::string& name = args[1];
        bool found = false;
        for (auto it = sched.entries.begin(); it != sched.entries.end(); ++it) {
            if (it->name == name) {
                sched.entries.erase(it);
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "error: no such entry: " << name << "\n";
            return 1;
        }
        return save_schedule(config, sched, opts.config_path);
    }

    sched.enabled = (sub == "enable");
    return save_schedule(config, sched, opts.config_path);
}

}  // namespace ed::cli
