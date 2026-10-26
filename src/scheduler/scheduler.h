// ElektroDump — time-of-day scheduler (spec §3.4, milestone M8).
// A 1 Hz loop selects the active schedule entry for the current time and asks
// App to swap the pipeline on a mode change. `tick()` runs one iteration for an
// injected time, so tests never sleep.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/pipeline.h"
#include "scheduler/schedule_model.h"

namespace ed {
class Config;
}

namespace ed::scheduler {

struct ScheduleState {
    bool enabled = true;
    std::string active_entry;         // "" when none
    std::string next_entry;
    std::string next_transition_utc;  // ISO-8601
    int64_t seconds_to_next = 0;
};

class Scheduler {
public:
    // Called on the scheduler thread on a mode change; `entry` null = default.
    using TransitionFn = std::function<void(PipelineMode, const ScheduleEntry*)>;

    // Janitor hook: called every `scheduler.janitor_seconds` on the loop.
    using JanitorFn = std::function<void()>;

    Scheduler();
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    bool configure(const Config& cfg, std::string* error);
    void set_transition(TransitionFn fn);
    void set_janitor(JanitorFn fn);

    void start();  // spawns the 1 Hz thread (T5)
    void stop();

    ScheduleState state() const;

    // Replace the schedule (from CLI or PUT /api/v1/schedule).
    bool apply(const Schedule& s, std::vector<FieldError>* errors);

    const Schedule& schedule() const;

    // Tell the scheduler the running mode so the first tick does not transition.
    void set_current_mode(PipelineMode mode);
    // Run one selection iteration for `now_utc` (seconds since the epoch).
    void tick(std::time_t now_utc);

private:
    void loop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    Schedule schedule_;
    ScheduleState state_;
    // Entry active at the previous tick ("" = none); stops the first tick from
    // forcing default_mode over the configured run.mode.
    std::string last_active_;
    TransitionFn transition_;
    JanitorFn janitor_;
    std::atomic<int> current_mode_{static_cast<int>(PipelineMode::GGAK)};
    int tick_seconds_ = 1;
    int janitor_seconds_ = 60;
    bool running_ = false;
    std::thread thread_;
};

}  // namespace ed::scheduler
