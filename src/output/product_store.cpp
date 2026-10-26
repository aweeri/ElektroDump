#include "output/product_store.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>

#include "core/config.h"
#include "core/time_util.h"
#include "output/metadata.h"
#include "util/fs_util.h"
#include "util/json_util.h"

namespace ed::output {

namespace {

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

// Extract YYYY/MM/DD from an id like "20261026T120000Z_visual".
bool date_parts(const std::string& id, std::string& year, std::string& month,
                std::string& day) {
    if (id.size() < 8) {
        return false;
    }
    year = id.substr(0, 4);
    month = id.substr(4, 2);
    day = id.substr(6, 2);
    return true;
}

void collect_files(const std::string& dir, const std::string& suffix,
                   std::vector<std::string>& out) {
    DIR* handle = opendir(dir.c_str());
    if (handle == nullptr) {
        return;
    }
    std::vector<std::string> names;
    while (struct dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        names.push_back(name);
    }
    closedir(handle);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const std::string path = fs::join(dir, name);
        if (fs::is_directory(path)) {
            collect_files(path, suffix, out);
        } else if (fs::is_regular_file(path) && has_suffix(name, suffix)) {
            out.push_back(path);
        }
    }
}

void remove_stale_tmp(const std::string& dir) {
    std::vector<std::string> files;
    collect_files(dir, ".tmp", files);
    for (const std::string& path : files) {
        ::unlink(path.c_str());
    }
}

}  // namespace

bool ProductStore::configure(const Config& cfg, std::string* error) {
    // `products.output_dir` is the offline CLI override; the live path uses storage.root.
    root_ = cfg.get_string("products.output_dir", "");
    if (root_.empty()) {
        root_ = cfg.get_string("storage.root", "data");
    }
    products_dir_ = fs::join(root_, "products");
    state_dir_ = fs::join(root_, "state");
    if (!fs::mkdir_p(products_dir_) || !fs::mkdir_p(state_dir_)) {
        if (error != nullptr) {
            *error = "cannot create product store under " + root_;
        }
        return false;
    }
    remove_stale_tmp(products_dir_);
    remove_stale_tmp(state_dir_);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::string ProductStore::relative_path(const ProductMeta& meta) const {
    std::string year;
    std::string month;
    std::string day;
    if (!date_parts(meta.id, year, month, day)) {
        year = "0000";
        month = "00";
        day = "00";
    }
    const std::string ext = meta.ext.empty() ? "webp" : meta.ext;
    return fs::join(
        "products",
        fs::join(year, fs::join(month,
                                fs::join(day,
                                         fs::join(meta.satellite,
                                                  fs::join(meta.product,
                                                           meta.id + "." + ext))))));
}

std::string ProductStore::image_path(const ProductMeta& meta) const {
    return fs::join(root_, meta.path);
}

bool ProductStore::save(const ProductMeta& meta, const std::string& bytes) {
    if (meta.id.empty() || meta.product.empty()) {
        return false;
    }
    const std::string rel =
        meta.path.empty() ? relative_path(meta) : meta.path;
    const std::string abs = fs::join(root_, rel);
    const std::string dir = fs::dirname(abs);
    if (!fs::mkdir_p(dir)) {
        return false;
    }
    if (!fs::atomic_write(abs, bytes)) {
        return false;
    }
    ProductMeta stored = meta;
    stored.path = rel;
    const std::string sidecar = fs::join(dir, meta.id + ".json");
    if (!fs::atomic_write(sidecar, metadata_to_json(stored))) {
        return false;
    }
    // Update state/latest.json atomically.
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string latest_path = fs::join(state_dir_, "latest.json");
    bool ok = false;
    const std::string text = fs::read_file(latest_path, &ok);
    json::Value root =
        ok ? json::Value::parse(text) : json::Value(json::Value::Object{});
    if (!root.is_object()) {
        root = json::Value(json::Value::Object{});
    }
    root.set(stored.product, json::Value::parse(metadata_to_json(stored)));
    return fs::atomic_write(latest_path, root.dump(2));
}

bool ProductStore::list(const std::string& type, const std::string& date,
                        int limit, std::vector<ProductMeta>& out) const {
    out.clear();
    std::vector<std::string> files;
    collect_files(products_dir_, ".json", files);
    for (const std::string& path : files) {
        bool ok = false;
        const std::string text = fs::read_file(path, &ok);
        if (!ok) {
            continue;
        }
        ProductMeta meta;
        if (!metadata_from_json(text, meta)) {
            continue;
        }
        if (!type.empty() && meta.product != type) {
            continue;
        }
        if (!date.empty()) {
            std::string year;
            std::string month;
            std::string day;
            if (!date_parts(meta.id, year, month, day)) {
                continue;
            }
            const std::string compact = year + month + day;
            const std::string dashed = year + "-" + month + "-" + day;
            if (date != compact && date != dashed) {
                continue;
            }
        }
        out.push_back(std::move(meta));
    }
    std::sort(out.begin(), out.end(),
              [](const ProductMeta& a, const ProductMeta& b) {
                  return a.id > b.id;
              });
    if (limit > 0 && static_cast<int>(out.size()) > limit) {
        out.resize(static_cast<std::size_t>(limit));
    }
    return true;
}

bool ProductStore::latest(const std::string& product, ProductMeta& out) const {
    const std::string latest_path = fs::join(state_dir_, "latest.json");
    bool ok = false;
    const std::string text = fs::read_file(latest_path, &ok);
    if (!ok) {
        return false;
    }
    const json::Value root = json::Value::parse(text);
    if (!root.is_object()) {
        return false;
    }
    const json::Value* entry = root.find(product);
    if (entry == nullptr || !entry->is_object()) {
        return false;
    }
    return metadata_from_json(entry->dump(0), out);
}

bool ProductStore::get(const std::string& id, ProductMeta& out) const {
    std::vector<std::string> files;
    collect_files(products_dir_, ".json", files);
    for (const std::string& path : files) {
        if (fs::basename(path) != id + ".json") {
            continue;
        }
        bool ok = false;
        const std::string text = fs::read_file(path, &ok);
        if (!ok) {
            continue;
        }
        return metadata_from_json(text, out);
    }
    return false;
}

int ProductStore::prune(int products_days) const {
    if (products_days < 1) {
        return 0;
    }
    std::vector<std::string> files;
    collect_files(products_dir_, ".json", files);
    std::vector<ProductMeta> metas;
    for (const std::string& path : files) {
        bool ok = false;
        const std::string text = fs::read_file(path, &ok);
        if (!ok) {
            continue;
        }
        ProductMeta meta;
        if (metadata_from_json(text, meta)) {
            metas.push_back(std::move(meta));
        }
    }
    std::sort(metas.begin(), metas.end(),
              [](const ProductMeta& a, const ProductMeta& b) {
                  return a.id > b.id;
              });
    // Keep the newest of each product type.
    std::vector<std::string> keep;
    std::vector<std::string> seen_products;
    for (const ProductMeta& meta : metas) {
        if (std::find(seen_products.begin(), seen_products.end(),
                      meta.product) == seen_products.end()) {
            seen_products.push_back(meta.product);
            keep.push_back(meta.id);
        }
    }
    const std::time_t now = std::time(nullptr);
    const std::time_t cutoff_time =
        now - static_cast<std::time_t>(products_days) * 86400;
    int year = 0;
    int month = 0;
    int day = 0;
    time::unix_to_ymd(cutoff_time, year, month, day);
    char cutoff_buf[16];
    std::snprintf(cutoff_buf, sizeof(cutoff_buf), "%04d%02d%02d", year, month,
                  day);
    const std::string cutoff(cutoff_buf);
    int deleted = 0;
    for (const ProductMeta& meta : metas) {
        if (std::find(keep.begin(), keep.end(), meta.id) != keep.end()) {
            continue;
        }
        if (meta.id.size() < 8 || meta.id.substr(0, 8) >= cutoff) {
            continue;
        }
        const std::string abs = fs::join(root_, meta.path);
        if (fs::is_regular_file(abs)) {
            ::unlink(abs.c_str());
        }
        const std::string sidecar =
            fs::join(fs::dirname(abs), meta.id + ".json");
        if (fs::is_regular_file(sidecar)) {
            ::unlink(sidecar.c_str());
        }
        ++deleted;
    }
    return deleted;
}

}  // namespace ed::output
