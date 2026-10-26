// ElektroDump — product store: save/list/latest/prune + latest index (spec §2.5).
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "output/product_meta.h"

namespace ed {
class Config;
}

namespace ed::output {

class ProductStore {
public:
    bool configure(const Config& cfg, std::string* error);

    // Write image bytes + JSON sidecar atomically, then update state/latest.json.
    bool save(const ProductMeta& meta, const std::string& bytes);

    bool list(const std::string& type, const std::string& date, int limit,
              std::vector<ProductMeta>& out) const;
    bool latest(const std::string& product, ProductMeta& out) const;
    bool get(const std::string& id, ProductMeta& out) const;

    // Absolute path of a product image (for the /image endpoint).
    std::string image_path(const ProductMeta& meta) const;

    // Relative path of a product image (for ProductMeta::path).
    std::string relative_path(const ProductMeta& meta) const;

    // Delete products older than `products_days`, never the latest of each type;
    // returns the number deleted.
    int prune(int products_days) const;

    const std::string& root() const { return root_; }

private:
    std::string root_;
    std::string products_dir_;
    std::string state_dir_;
    mutable std::mutex mutex_;  // guards latest.json rewrites
};

}  // namespace ed::output
