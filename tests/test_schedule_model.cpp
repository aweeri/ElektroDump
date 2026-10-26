// ElektroDump — M8 self-tests (schedule model: parse/validate/serialise).
#include <cstdio>
#include <string>
#include <vector>

#include "scheduler/schedule_model.h"
#include "util/json_util.h"

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

void test_parse_valid() {
    const char* text = R"({
      "enabled": true,
      "timezone": "UTC",
      "default_mode": "ggak",
      "entries": [
        {"name":"a","start":"06:00","end":"06:20","mode":"lrit",
         "priority":10,"frequency_hz":1691000000,"symbol_rate":294000,
         "sample_rate_hz":1000000}
      ]
    })";
    std::string err;
    const ed::json::Value root = ed::json::Value::parse(text, &err);
    CHECK(err.empty());

    std::vector<ed::scheduler::FieldError> errors;
    const ed::scheduler::Schedule s =
        ed::scheduler::parse_schedule(root, &errors);
    CHECK(errors.empty());
    CHECK(s.enabled);
    CHECK(s.timezone == "UTC");
    CHECK(s.default_mode == "ggak");
    CHECK(s.entries.size() == 1);
    CHECK(s.entries[0].name == "a");
    CHECK(s.entries[0].start_minute_of_day == 360);
    CHECK(s.entries[0].end_minute_of_day == 380);
    CHECK(s.entries[0].mode == "lrit");
    CHECK(s.entries[0].priority == 10);
    CHECK(s.entries[0].frequency_hz == 1691000000ULL);
    CHECK(s.entries[0].symbol_rate == 294000.0);
    CHECK(s.entries[0].sample_rate_hz == 1000000U);
    std::printf("  parse valid: 1 entry, start=%d end=%d\n",
                s.entries[0].start_minute_of_day, s.entries[0].end_minute_of_day);
}

void test_round_trip() {
    ed::scheduler::Schedule s;
    s.enabled = false;
    s.timezone = "UTC";
    s.default_mode = "hrit";
    ed::scheduler::ScheduleEntry e;
    e.name = "wrap";
    e.start_minute_of_day = 23 * 60;
    e.end_minute_of_day = 60;
    e.mode = "hrit";
    e.priority = 3;
    e.frequency_hz = 1691000000;
    e.symbol_rate = 1150000.0;
    e.sample_rate_hz = 6000000;
    s.entries.push_back(e);

    const ed::json::Value out = ed::scheduler::schedule_to_json(s);
    std::vector<ed::scheduler::FieldError> errors;
    const ed::scheduler::Schedule s2 =
        ed::scheduler::parse_schedule(out, &errors);
    CHECK(errors.empty());
    CHECK(!s2.enabled);
    CHECK(s2.default_mode == "hrit");
    CHECK(s2.entries.size() == 1);
    CHECK(s2.entries[0].start_minute_of_day == 23 * 60);
    CHECK(s2.entries[0].end_minute_of_day == 60);
    CHECK(s2.entries[0].frequency_hz == 1691000000ULL);
    CHECK(s2.entries[0].sample_rate_hz == 6000000U);
    // The serialised form must keep "HH:MM" strings for Config::validate.
    const ed::json::Value* start = out.find("entries");
    CHECK(start != nullptr && start->is_array());
    const ed::json::Value* first = &(*start)[0];
    const ed::json::Value* start_field = first->find("start");
    CHECK(start_field != nullptr && start_field->is_string());
    CHECK(start_field->as_string() == "23:00");
    std::printf("  round-trip: start=%s end=%s\n",
                start_field->as_string().c_str(),
                first->find("end")->as_string().c_str());
}

void test_validate_errors() {
    ed::scheduler::Schedule bad;
    bad.timezone = "UTC";
    bad.default_mode = "ggak";
    ed::scheduler::ScheduleEntry e1;
    e1.name = "x";
    e1.start_minute_of_day = 0;
    e1.end_minute_of_day = 0;  // start == end
    e1.mode = "lrit";
    ed::scheduler::ScheduleEntry e2;
    e2.name = "x";  // duplicate
    e2.start_minute_of_day = 10;
    e2.end_minute_of_day = 20;
    e2.mode = "foo";  // bad mode
    bad.entries = {e1, e2};

    std::vector<ed::scheduler::FieldError> errors;
    CHECK(!ed::scheduler::validate_schedule(bad, &errors));
    CHECK(errors.size() >= 3);
    std::printf("  validate errors: %zu field error(s)\n", errors.size());

    // A valid schedule passes.
    ed::scheduler::Schedule good;
    good.timezone = "UTC";
    good.default_mode = "ggak";
    ed::scheduler::ScheduleEntry ok;
    ok.name = "ok";
    ok.start_minute_of_day = 60;
    ok.end_minute_of_day = 120;
    ok.mode = "lrit";
    good.entries = {ok};
    std::vector<ed::scheduler::FieldError> ok_errors;
    CHECK(ed::scheduler::validate_schedule(good, &ok_errors));
    CHECK(ok_errors.empty());
}

void test_parse_errors() {
    const char* text =
        R"({"entries":[{"name":"b","start":"25:00","end":"06:00","mode":"lrit"}]})";
    std::string err;
    const ed::json::Value root = ed::json::Value::parse(text, &err);
    CHECK(err.empty());
    std::vector<ed::scheduler::FieldError> errors;
    ed::scheduler::parse_schedule(root, &errors);
    CHECK(!errors.empty());
    std::printf("  parse errors: %zu field error(s)\n", errors.size());
}

}  // namespace

int main() {
    test_parse_valid();
    test_round_trip();
    test_validate_errors();
    test_parse_errors();

    if (g_failures == 0) {
        std::printf("all M8 schedule-model tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
