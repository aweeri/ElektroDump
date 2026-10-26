#include "scheduler/scheduler.h"

#include <chrono>
#include <cstdio>

#include "core/config.h"
#include "core/logging.h"
#include "core/time_util.h"
#include "util/string_util.h"

namespace ed::scheduler {

namespace {

PipelineMode mode_from_string(const std::string& mode) {
    const std::string m = str::to_lower(mode);
    if (m == "lrit") {
        return PipelineMode::LRIT;
    }
    if (m == "hrit") {
        return PipelineMode::HRIT;
    }
    return PipelineMode::GGAK;
}

// Minute-of-day in the schedule timezone. Supports "UTC", "local" and fixed
// offsets "UTC+H"/"UTC-H"/"UTC+H:MM"; anything else is UTC.
int minute_of_day(std::time_t now, const std::string& tz) {
    std::tm tm {};
    if (tz == "local") {
        ::localtime_r(&now, &tm);
        return tm.tm_hour * 60 + tm.tm_min;
    }
    ::gmtime_r(&now, &tm);
    int offset_minutes = 0;
    if (tz.size() > 3 && tz.compare(0, 3, "UTC") == 0) {
        char sign = 0;
        int hh = 0;
        int mm = 0;
        if (std::sscanf(tz.c_str() + 3, "%c%d:%d", &sign, &hh, &mm) >= 2 &&
            (sign == '+' || sign == '-')) {
            offset_minutes = hh * 60 + mm;
            if (sign == '-') {
                offset_minutes = -offset_minutes;
            }
        }
    }
    int mod = tm.tm_hour * 60 + tm.tm_min + offset_minutes;
    mod %= 1440;
    if (mod < 0) {
        mod += 1440;
    }
    return mod;
}

bool entry_active(const ScheduleEntry& e, int mod) {
    if (e.start_minute_of_day < e.end_minute_of_day) {
        return mod >= e.start_minute_of_day && mod < e.end_minute_of_day;
    }
    // Wraps midnight (e.g. 23:00 -> 01:00).
    return mod >= e.start_minute_of_day || mod < e.end_minute_of_day;
}

}  // namespace

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() { stop(); }

bool Scheduler::configure(const Config& cfg, std::string* error) {
    const json::Value* sched = cfg.root().find_path("schedule");
    std::vector<FieldError> errors;
    const Schedule parsed =
        parse_schedule(sched != nullptr ? *sched : json::Value(), &errors);
    if (!errors.empty()) {
        if (error != nullptr) {
            *error = errors.front().path + ": " + errors.front().message;
        }
        return false;
    }
    std::vector<FieldError> verrs;
    if (!validate_schedule(parsed, &verrs)) {
        if (error != nullptr) {
            *error = verrs.front().path + ": " + verrs.front().message;
        }
        return false;
    }

    int tick = static_cast<int>(cfg.get_int("scheduler.tick_seconds", 1));
    if (tick < 1) {
        tick = 1;
    }
    int janitor = static_cast<int>(cfg.get_int("scheduler.janitor_seconds", 60));
    if (janitor < 1) {
        janitor = 1;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    schedule_ = parsed;
    tick_seconds_ = tick;
    janitor_seconds_ = janitor;
    state_.enabled = parsed.enabled;
    last_active_.clear();
    current_mode_.store(static_cast<int>(mode_from_string(parsed.default_mode)),
                        std::memory_order_relaxed);
    return true;
}

void Scheduler::set_transition(TransitionFn fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    transition_ = std::move(fn);
}

void Scheduler::set_janitor(JanitorFn fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    janitor_ = std::move(fn);
}

void Scheduler::set_current_mode(PipelineMode mode) {
    current_mode_.store(static_cast<int>(mode), std::memory_order_relaxed);
}

void Scheduler::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }
        running_ = true;
    }
    thread_ = std::thread([this] { loop(); });
}

void Scheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Scheduler::loop() {
    auto next_janitor = std::chrono::steady_clock::now() +
                        std::chrono::seconds(janitor_seconds_);
    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                break;
            }
        }
        tick(std::time(nullptr));

        if (std::chrono::steady_clock::now() >= next_janitor) {
            JanitorFn janitor;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                janitor = janitor_;
            }
            if (janitor) {
                janitor();
            }
            next_janitor = std::chrono::steady_clock::now() +
                           std::chrono::seconds(janitor_seconds_);
        }

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(tick_seconds_),
                     [this] { return !running_; });
    }
}

void Scheduler::tick(std::time_t now_utc) {
    Schedule sched;
    TransitionFn fn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sched = schedule_;
        fn = transition_;
    }

    const int mod = minute_of_day(now_utc, sched.timezone);

    const ScheduleEntry* active = nullptr;
    if (sched.enabled) {
        for (const ScheduleEntry& e : sched.entries) {
            if (!entry_active(e, mod)) {
                continue;
            }
            if (active == nullptr || e.priority > active->priority) {
                active = &e;
            }
        }
    }
    const PipelineMode target =
        active != nullptr ? mode_from_string(active->mode)
                          : mode_from_string(sched.default_mode);

    const std::string active_name =
        active != nullptr ? active->name : std::string();
    const bool entry_changed = active_name != last_active_;
    last_active_ = active_name;

    const PipelineMode current =
        static_cast<PipelineMode>(current_mode_.load(std::memory_order_relaxed));
    // Transition when the desired mode differs from the running one, but only
    // when the active entry changed (or the schedule is disabled), so the
    // configured run.mode survives startup when no entry is active.
    if (fn && target != current && (entry_changed || !sched.enabled)) {
        ScheduleEntry copy;
        const ScheduleEntry* arg = nullptr;
        if (active != nullptr) {
            copy = *active;
            arg = &copy;
        }
        fn(target, arg);
        current_mode_.store(static_cast<int>(target), std::memory_order_relaxed);
    }

    // Next boundary across all entries (start or end), in seconds.
    int64_t best = -1;
    std::string next_name;
    const int now_sod = mod * 60 + static_cast<int>(now_utc % 60);
    for (const ScheduleEntry& e : sched.entries) {
        const int boundaries[2] = {e.start_minute_of_day, e.end_minute_of_day};
        for (int boundary : boundaries) {
            int64_t delta = static_cast<int64_t>(boundary) * 60 - now_sod;
            if (delta <= 0) {
                delta += 86400;
            }
            if (best < 0 || delta < best) {
                best = delta;
                next_name = e.name;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    state_.enabled = sched.enabled;
    state_.active_entry = active_name;
    state_.next_entry = next_name;
    state_.seconds_to_next = best < 0 ? 0 : best;
    state_.next_transition_utc =
        best < 0 ? std::string()
                 : time::iso8601(time::now_utc() + std::chrono::seconds(best),
                                 false);
}

ScheduleState Scheduler::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool Scheduler::apply(const Schedule& s, std::vector<FieldError>* errors) {
    if (!validate_schedule(s, errors)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    schedule_ = s;
    state_.enabled = s.enabled;
    last_active_.clear();
    return true;
}

const Schedule& Scheduler::schedule() const {
    // Return a thread-local copy so callers never observe a concurrent apply().
    static thread_local Schedule copy;
    std::lock_guard<std::mutex> lock(mutex_);
    copy = schedule_;
    return copy;
}

}  // namespace ed::scheduler
