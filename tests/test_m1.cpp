// ElektroDump — M1 self-tests (JSON, config, time, strings, stats, fs).
#include <cstdio>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/time_util.h"
#include "util/fs_util.h"
#include "util/json_util.h"
#include "util/stats.h"
#include "util/string_util.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                         #cond);                                          \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

void test_json() {
    std::string err;
    const auto value = ed::json::Value::parse(
        R"({"a":1,"b":[true,null,"x"],"c":{"d":2.5}})", &err);
    CHECK(err.empty());
    CHECK(value.is_object());
    CHECK(value["a"].as_int() == 1);
    CHECK(value["b"].size() == 3);
    CHECK(value["b"][0].as_bool());
    CHECK(value["b"][1].is_null());
    CHECK(value["c"]["d"].as_double() == 2.5);

    const auto round = ed::json::Value::parse(value.dump(2), &err);
    CHECK(err.empty());
    CHECK(round["a"].as_int() == 1);
    CHECK(round["c"]["d"].as_double() == 2.5);

    // Invalid JSON must report an error.
    ed::json::Value::parse("{bad}", &err);
    CHECK(!err.empty());
}

void test_deep_merge() {
    std::string err;
    auto base = ed::json::Value::parse(R"({"a":1,"b":{"c":2,"d":3}})", &err);
    const auto overlay =
        ed::json::Value::parse(R"({"b":{"c":9},"e":5})", &err);
    ed::json::deep_merge(base, overlay);
    CHECK(base["b"]["c"].as_int() == 9);
    CHECK(base["b"]["d"].as_int() == 3);
    CHECK(base["e"].as_int() == 5);
}

void test_config() {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    auto defaults = ed::Config::defaults();
    CHECK(defaults.validate(&errors, &warnings));
    CHECK(errors.empty());
    CHECK(defaults.get_string("sdr.driver") == "airspy");
    CHECK(defaults.get_int("frequencies.ggak_hz") == 1693000000);

    // User override: switching to RTL-SDR must make HRIT invalid.
    ed::fs::mkdir_p("build");
    const std::string path = "build/test_config.json";
    CHECK(ed::fs::atomic_write(
        path, R"({"version":1,"sdr":{"driver":"rtlsdr"}})"));
    std::string load_error;
    auto loaded = ed::Config::load(path, &load_error);
    CHECK(load_error.empty());
    CHECK(loaded.loaded_from_file());
    CHECK(loaded.get_string("sdr.driver") == "rtlsdr");
    CHECK(!loaded.validate(&errors, &warnings));
    ed::fs::remove_all(path);
}

void test_time() {
    bool ok = false;
    CHECK(ed::time::parse_hhmm("06:20", &ok) == 380);
    CHECK(ok);
    CHECK(ed::time::parse_hhmm("24:00", &ok) == -1);
    CHECK(!ok);
    CHECK(ed::time::parse_hhmm("nope", &ok) == -1);
    CHECK(!ok);
}

void test_strings() {
    CHECK(ed::str::trim("  hi  ") == "hi");
    CHECK(ed::str::split("a,b,c", ',').size() == 3);
    CHECK(ed::str::replace_all("a-b-c", "-", "_") == "a_b_c");
    CHECK(ed::str::starts_with("elektrodump", "elektro"));
    CHECK(ed::str::ends_with("elektrodump", "dump"));
}

void test_stats() {
    ed::stats::RunningStats stats;
    stats.add(1.0);
    stats.add(2.0);
    stats.add(3.0);
    CHECK(stats.count() == 3);
    CHECK(stats.mean() == 2.0);
    CHECK(stats.min() == 1.0);
    CHECK(stats.max() == 3.0);
    CHECK(ed::stats::median({3.0, 1.0, 2.0}) == 2.0);
}

}  // namespace

int main() {
    test_json();
    test_deep_merge();
    test_config();
    test_time();
    test_strings();
    test_stats();

    if (g_failures == 0) {
        std::printf("all M1 tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
