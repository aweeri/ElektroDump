// ElektroDump — M7b self-tests (product store save/list/latest/prune).
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/config.h"
#include "output/product_store.h"
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

ed::Config make_config(const std::string& root) {
    ed::Config cfg = ed::Config::defaults();
    cfg.root()["products"].set("output_dir", ed::json::Value(root));
    return cfg;
}

ed::output::ProductMeta make_meta(const std::string& id,
                                  const std::string& product) {
    ed::output::ProductMeta meta;
    meta.id = id;
    meta.satellite = "elektro-l3";
    meta.product = product;
    meta.projection = "mercator";
    meta.width = 4;
    meta.height = 4;
    meta.created = "2026-10-26T12:00:00Z";
    meta.ext = "ppm";
    meta.geo.valid = true;
    meta.geo.lon0_deg = 76.0;
    meta.geo.altitude_m = 35786023.0;
    meta.geo.lon_min = -5.0;
    meta.geo.lon_max = 157.0;
    meta.geo.lat_min = -81.0;
    meta.geo.lat_max = 81.0;
    return meta;
}

void test_save_latest_list_get() {
    const std::string root = "build/m7_store_test";
    ed::fs::remove_all(root);
    ed::Config cfg = make_config(root);
    ed::output::ProductStore store;
    std::string error;
    CHECK(store.configure(cfg, &error));
    CHECK(error.empty());

    ed::output::ProductMeta meta =
        make_meta("20261026T120000Z_visual", "visual");
    meta.path = store.relative_path(meta);
    CHECK(store.save(meta, "P6\n4 4\n255\n...."));

    const std::string path = store.image_path(meta);
    CHECK(ed::fs::is_regular_file(path));
    CHECK(path.find("products/2026/10/26/elektro-l3/visual/") !=
          std::string::npos);

    ed::output::ProductMeta latest;
    CHECK(store.latest("visual", latest));
    CHECK(latest.id == meta.id);
    CHECK(latest.geo.valid);
    CHECK(latest.geo.lon0_deg == 76.0);

    std::vector<ed::output::ProductMeta> products;
    CHECK(store.list("visual", "", 10, products));
    CHECK(products.size() == 1);
    CHECK(products[0].id == meta.id);

    ed::output::ProductMeta got;
    CHECK(store.get(meta.id, got));
    CHECK(got.id == meta.id);
    CHECK(got.product == "visual");
}

void test_prune() {
    const std::string root = "build/m7_store_prune";
    ed::fs::remove_all(root);
    ed::Config cfg = make_config(root);
    ed::output::ProductStore store;
    std::string error;
    CHECK(store.configure(cfg, &error));

    ed::output::ProductMeta old_meta =
        make_meta("20200101T000000Z_visual", "visual");
    ed::output::ProductMeta new_meta =
        make_meta("20261026T120000Z_visual", "visual");
    old_meta.path = store.relative_path(old_meta);
    new_meta.path = store.relative_path(new_meta);
    CHECK(store.save(old_meta, "old"));
    CHECK(store.save(new_meta, "new"));

    const int deleted = store.prune(30);
    CHECK(deleted == 1);
    CHECK(!ed::fs::is_regular_file(store.image_path(old_meta)));
    CHECK(ed::fs::is_regular_file(store.image_path(new_meta)));
}

void test_stale_tmp_cleanup() {
    const std::string root = "build/m7_store_tmp";
    ed::fs::remove_all(root);
    ed::Config cfg = make_config(root);
    ed::output::ProductStore store;
    std::string error;
    CHECK(store.configure(cfg, &error));
    const std::string stale = ed::fs::join(root, "products/stale.tmp");
    CHECK(ed::fs::write_file(stale, "stale"));
    CHECK(ed::fs::is_regular_file(stale));
    CHECK(store.configure(cfg, &error));
    CHECK(!ed::fs::is_regular_file(stale));
}

}  // namespace

int main() {
    test_save_latest_list_get();
    test_prune();
    test_stale_tmp_cleanup();

    if (g_failures == 0) {
        std::printf("all M7b product store tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
