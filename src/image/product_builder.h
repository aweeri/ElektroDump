// ElektroDump — worker-thread bridge from decode to image products (spec §0.3).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "image/calibration.h"
#include "image/image.h"
#include "image/image_assembler.h"
#include "output/product_meta.h"
#include "output/product_store.h"
#include "projection/projection.h"
#include "xrit/xrit_file.h"

namespace ed {
class Config;
}

namespace ed::image {

class ProductBuilder {
public:
    ProductBuilder();
    ~ProductBuilder();

    ProductBuilder(const ProductBuilder&) = delete;
    ProductBuilder& operator=(const ProductBuilder&) = delete;

    // Load calibration, LUTs, projection config and the product store.
    bool configure(const Config& cfg, std::string* error);

    // Called from the decode thread (T2). Non-blocking: enqueues the file.
    void submit(const xrit::XritFile& file);

    // Called when a capture ends so partial channels are flushed.
    void flush();

    void start();  // spawns the worker thread
    void stop();   // joins, flushes

    struct Stats {
        uint64_t channels_assembled = 0;
        uint64_t products_written = 0;
        uint64_t products_failed = 0;
        uint64_t segments_missing = 0;
    };
    Stats stats() const;

    // Set by App: invoked on the worker thread after a product is published.
    using ProductCallback = std::function<void(const output::ProductMeta&)>;
    void set_product_callback(ProductCallback cb);

    // Offline CLI override for the product directory.
    void set_output_dir(const std::string& dir) { output_dir_ = dir; }

    // The store the builder writes to (also handed to the web API).
    output::ProductStore* store() { return &store_; }

private:
    struct Item {
        bool flush = false;
        xrit::XritFile file;
    };

    void worker_loop();
    void handle_channel(AssembledChannel&& channel);
    void build_products();
    std::string choose_ext() const;
    bool encode_rgb(const Image8& image, const std::string& ext,
                    std::string& bytes) const;
    bool encode_rgba(const ImageRGBA& image, const std::string& ext,
                     std::string& bytes) const;

    const Config* config_ = nullptr;
    std::unique_ptr<ImageAssembler> assembler_;
    CalibrationTable calibration_;
    std::string output_dir_;
    int max_output_dimension_ = 2048;
    int queue_depth_ = 64;
    bool calibration_enabled_ = true;

    output::ProductStore store_;
    projection::Projector projector_;
    std::string projection_type_ = "mercator";
    int projection_width_ = 2048;
    int projection_height_ = 2048;
    int webp_quality_ = 85;
    int webp_method_ = 4;

    std::map<int, AssembledChannel> pending_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Item> queue_;
    std::thread thread_;
    bool running_ = false;
    bool stop_ = false;

    ProductCallback callback_;

    std::atomic<uint64_t> channels_assembled_{0};
    std::atomic<uint64_t> products_written_{0};
    std::atomic<uint64_t> products_failed_{0};
    std::atomic<uint64_t> segments_missing_{0};
};

}  // namespace ed::image
