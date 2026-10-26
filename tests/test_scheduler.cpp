// ElektroDump — M8 self-tests (scheduler: selection, priority, transitions).
//
// The scheduler is driven with an injected time via tick(), so the tests are
// fully deterministic and never sleep.
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "core/config.h"
#include "scheduler/scheduler.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                         #cond);                                         \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

std::time_t utc(int year, int month, int day, int hour, int minute) {
    std::tm tm {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    return ::timegm(&tm);
}

ed::scheduler::ScheduleEntry make_entry(const std::string& name, int start,
                                        int end, const std::string& mode,
                                        int priority) {
    ed::scheduler::ScheduleEntry e;
    e.name = name;
    e.start_minute_of_day = start;
    e.end_minute_of_day = end;
    e.mode = mode;
    e.priority = priority;
    return e;
}

void test_transitions() {
    ed::scheduler::Scheduler sched;
    std::string err;
    CHECK(sched.configure(ed::Config::defaults(), &err));

    ed::scheduler::Schedule s;
    s.enabled = true;
    s.timezone = "UTC";
    s.default_mode = "ggak";
    s.entries.push_back(make_entry("morning", 6 * 60, 6 * 60 + 20, "lrit", 10));
    std::vector<ed::scheduler::FieldError> ferr;
    CHECK(sched.apply(s, &ferr));

    sched.set_current_mode(ed::PipelineMode::GGAK);
    std::vector<std::pair<ed::PipelineMode, std::string>> calls;
    sched.set_transition([&calls](ed::PipelineMode m,
                                  const ed::scheduler::ScheduleEntry* e) {
        calls.emplace_back(m, e != nullptr ? e->name : std::string());
    });

    // 05:59 -> still GGAK, no transition.
    sched.tick(utc(2026, 10, 26, 5, 59));
    CHECK(calls.empty());
    CHECK(sched.state().active_entry.empty());

    // 06:00 -> LRIT (entry "morning").
    sched.tick(utc(2026, 10, 26, 6, 0));
    CHECK(calls.size() == 1);
    CHECK(calls[0].first == ed::PipelineMode::LRIT);
    CHECK(calls[0].second == "morning");
    CHECK(sched.state().active_entry == "morning");

    // 06:10 -> still LRIT, no new transition.
    sched.tick(utc(2026, 10, 26, 6, 10));
    CHECK(calls.size() == 1);

    // 06:20 -> back to GGAK (default).
    sched.tick(utc(2026, 10, 26, 6, 20));
    CHECK(calls.size() == 2);
    CHECK(calls[1].first == ed::PipelineMode::GGAK);
    CHECK(calls[1].second.empty());
    CHECK(sched.state().active_entry.empty());

    // The next transition is the 06:00 start (tomorrow) from 06:20.
    CHECK(sched.state().seconds_to_next > 0);
    CHECK(sched.state().next_entry == "morning");
    std::printf("  transitions: %zu call(s), next in %lld s\n", calls.size(),
                static_cast<long long>(sched.state().seconds_to_next));
}

void test_priority() {
    ed::scheduler::Scheduler sched;
    std::string err;
    CHECK(sched.configure(ed::Config::defaults(), &err));

    ed::scheduler::Schedule s;
    s.enabled = true;
    s.timezone = "UTC";
    s.default_mode = "ggak";
    s.entries.push_back(make_entry("low", 6 * 60, 7 * 60, "lrit", 1));
    s.entries.push_back(make_entry("high", 6 * 60 + 30, 6 * 60 + 40, "hrit", 5));
    std::vector<ed::scheduler::FieldError> ferr;
    CHECK(sched.apply(s, &ferr));

    sched.set_current_mode(ed::PipelineMode::GGAK);
    std::vector<std::pair<ed::PipelineMode, std::string>> calls;
    sched.set_transition([&calls](ed::PipelineMode m,
                                  const ed::scheduler::ScheduleEntry* e) {
        calls.emplace_back(m, e != nullptr ? e->name : std::string());
    });

    // 06:10 -> only "low" is active.
    sched.tick(utc(2026, 10, 26, 6, 10));
    CHECK(calls.size() == 1);
    CHECK(calls[0].second == "low");
    CHECK(calls[0].first == ed::PipelineMode::LRIT);

    // 06:35 -> both active, "high" wins.
    sched.tick(utc(2026, 10, 26, 6, 35));
    CHECK(calls.size() == 2);
    CHECK(calls[1].second == "high");
    CHECK(calls[1].first == ed::PipelineMode::HRIT);
    std::printf("  priority: %s wins on overlap\n", calls[1].second.c_str());
}

void test_disabled_and_wrap() {
    ed::scheduler::Scheduler sched;
    std::string err;
    CHECK(sched.configure(ed::Config::defaults(), &err));

    ed::scheduler::Schedule s;
    s.enabled = false;  // disabled -> always default_mode
    s.timezone = "UTC";
    s.default_mode = "hrit";
    s.entries.push_back(make_entry("night", 23 * 60, 60, "lrit", 1));
    std::vector<ed::scheduler::FieldError> ferr;
    CHECK(sched.apply(s, &ferr));

    sched.set_current_mode(ed::PipelineMode::GGAK);
    std::vector<ed::PipelineMode> calls;
    sched.set_transition([&calls](ed::PipelineMode m,
                                  const ed::scheduler::ScheduleEntry*) {
        calls.push_back(m);
    });

    sched.tick(utc(2026, 10, 26, 23, 30));
    CHECK(calls.size() == 1);
    CHECK(calls[0] == ed::PipelineMode::HRIT);
    CHECK(sched.state().active_entry.empty());

    // Enable and check the midnight-wrapping entry is active at 00:30.
    s.enabled = true;
    CHECK(sched.apply(s, &ferr));
    sched.tick(utc(2026, 10, 27, 0, 30));
    CHECK(calls.size() == 2);
    CHECK(calls[1] == ed::PipelineMode::LRIT);
    CHECK(sched.state().active_entry == "night");
    std::printf("  disabled/wrap: default=%d wrap-active=%s\n",
                static_cast<int>(calls[0]),
                sched.state().active_entry.c_str());
}

}  // namespace

int main() {
    test_transitions();
    test_priority();
    test_disabled_and_wrap();

    if (g_failures == 0) {
        std::printf("all M8 scheduler tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
