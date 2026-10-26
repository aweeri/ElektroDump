// ElektroDump — `image <xrit-dir>` (spec §1.8, milestone M6).
// Offline image assembly: read assembled xRIT files, stitch them into full-disk
// channels, calibrate and write the visual/ir/ir_cloud products.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>

#include "cli/cli.h"
#include "core/config.h"
#include "image/product_builder.h"
#include "util/fs_util.h"
#include "util/json_util.h"
#include "util/string_util.h"
#include "xrit/xrit_file.h"

namespace ed::cli {

namespace {

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

void collect_lrit(const std::string& dir, std::vector<std::string>& out) {
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
            if (name != "products") {
                collect_lrit(path, out);
            }
        } else if (fs::is_regular_file(path) && has_suffix(name, ".lrit")) {
            out.push_back(path);
        }
    }
}

}  // namespace

int cmd_image(const Options& opts, const Args& args) {
    if (args.empty()) {
        std::cerr << "error: image requires an xRIT directory\n";
        return 2;
    }
    const std::string dir = args[0];
    if (!fs::is_directory(dir)) {
        std::cerr << "error: not a directory: " << dir << "\n";
        return 2;
    }

    std::string config_error;
    Config cfg = Config::load(opts.config_path, &config_error);
    if (!config_error.empty()) {
        std::cerr << "warning: " << config_error << " (using defaults)\n";
    }

    const std::string out_dir =
        args.size() > 1 ? args[1] : fs::join(dir, "products");
    cfg.root()["products"].set("output_dir", json::Value(out_dir));

    image::ProductBuilder builder;
    std::string error;
    if (!builder.configure(cfg, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    builder.start();

    std::vector<std::string> files;
    collect_lrit(dir, files);
    for (const std::string& path : files) {
        bool ok = false;
        const std::string bytes = fs::read_file(path, &ok);
        if (!ok) {
            std::cerr << "warning: cannot read " << path << "\n";
            continue;
        }
        xrit::XritFile file;
        file.data.assign(bytes.begin(), bytes.end());
        if (!file.parse_headers()) {
            std::cerr << "warning: not an xRIT file: " << path << "\n";
            continue;
        }
        file.header_parsed = true;
        builder.submit(file);
    }
    builder.flush();
    builder.stop();

    const image::ProductBuilder::Stats stats = builder.stats();
    std::cout << "image: " << files.size() << " file(s), "
              << stats.channels_assembled << " channel(s), "
              << stats.products_written << " product(s)";
    if (stats.products_failed > 0) {
        std::cout << ", " << stats.products_failed << " failed";
    }
    std::cout << "\noutput: " << out_dir << "\n";
    return 0;
}

}  // namespace ed::cli
