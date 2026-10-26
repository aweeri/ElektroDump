// ElektroDump — M8 self-tests (CADU spool lifecycle: create/delete/retain).
#include <cstdio>
#include <ctime>
#include <string>

#include "core/config.h"
#include "scheduler/cadu_lifecycle.h"
#include "util/fs_util.h"
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

ed::Config make_config(const std::string& root, int failed_hours) {
    ed::Config cfg = ed::Config::defaults();
    ed::json::Value* storage_root = cfg.root().find_path("storage.root");
    if (storage_root != nullptr) {
        *storage_root = ed::json::Value(root);
    }
    ed::json::Value* failed =
        cfg.root().find_path("storage.retention.failed_cadu_hours");
    if (failed != nullptr) {
        *failed = ed::json::Value(static_cast<int64_t>(failed_hours));
    }
    return cfg;
}

void test_lifecycle() {
    const std::string root = "build/m8_cadu_test";
    ed::fs::remove_all(root);

    ed::scheduler::CaduLifecycle cadu;
    std::string err;
    CHECK(cadu.configure(make_config(root, 1), &err));

    // Successful capture: spool created then deleted.
    const std::string path1 =
        cadu.begin_capture(ed::PipelineMode::LRIT, "cap1");
    CHECK(ed::fs::is_directory(path1));
    CHECK(cadu.stats().spools_active == 1);
    cadu.end_capture("cap1", true);
    CHECK(!ed::fs::exists(path1));
    CHECK(cadu.stats().spools_deleted == 1);
    CHECK(cadu.stats().spools_active == 0);

    // Failed capture: spool retained.
    const std::string path2 =
        cadu.begin_capture(ed::PipelineMode::HRIT, "cap2");
    CHECK(ed::fs::is_directory(path2));
    cadu.end_capture("cap2", false);
    CHECK(ed::fs::exists(path2));
    CHECK(cadu.stats().spools_retained == 1);
    CHECK(cadu.stats().spools_active == 0);

    // Janitor before the retention window keeps it.
    cadu.janitor_at(std::time(nullptr));
    CHECK(ed::fs::exists(path2));

    // Janitor after the retention window (1 h) deletes it.
    cadu.janitor_at(std::time(nullptr) + 2 * 3600);
    CHECK(!ed::fs::exists(path2));
    CHECK(cadu.stats().spools_deleted == 2);

    std::printf("  lifecycle: deleted=%llu retained=%llu\n",
                static_cast<unsigned long long>(cadu.stats().spools_deleted),
                static_cast<unsigned long long>(cadu.stats().spools_retained));

    ed::fs::remove_all(root);
}

}  // namespace

int main() {
    test_lifecycle();

    if (g_failures == 0) {
        std::printf("all M8 CADU lifecycle tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
