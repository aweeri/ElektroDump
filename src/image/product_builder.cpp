#include "image/product_builder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#ifdef ED_HAVE_PNG
#include <png.h>
#endif

#include "core/config.h"
#include "core/logging.h"
#include "core/thread_util.h"
#include "core/time_util.h"
#include "image/composite.h"
#include "image/image_ops.h"
#include "output/webp_writer.h"
#include "util/fs_util.h"

namespace ed::image {

namespace {

#ifdef ED_HAVE_PNG
Image8 load_png_rgb(const std::string& path) {
    Image8 out;
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str())) {
        return out;
    }
    image.format = PNG_FORMAT_RGB;
    std::vector<uint8_t> buffer(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, buffer.data(), 0, nullptr)) {
        png_image_free(&image);
        return out;
    }
    out.width = static_cast<int>(image.width);
    out.height = static_cast<int>(image.height);
    out.channels = 3;
    out.data = std::move(buffer);
    png_image_free(&image);
    return out;
}
#endif

projection::GeoTransform scaled_geotransform(const projection::GeoTransform& gt,
                                             int out_w, int out_h) {
    projection::GeoTransform scaled = gt;
    if (gt.src_width > 0 && gt.src_height > 0) {
        const double sx = static_cast<double>(out_w) / gt.src_width;
        const double sy = static_cast<double>(out_h) / gt.src_height;
        scaled.column_scalar *= sx;
        scaled.line_scalar *= sy;
        scaled.column_offset *= sx;
        scaled.line_offset *= sy;
    }
    scaled.src_width = out_w;
    scaled.src_height = out_h;
    return scaled;
}

}  // namespace

ProductBuilder::ProductBuilder() = default;

ProductBuilder::~ProductBuilder() { stop(); }

bool ProductBuilder::configure(const Config& cfg, std::string* error) {
    config_ = &cfg;
    assembler_ = std::make_unique<ImageAssembler>(cfg);
    calibration_enabled_ = cfg.get_bool("calibration.enabled", true);
    calibration_ = CalibrationTable{};
    if (calibration_enabled_) {
        std::string calibration_error;
        calibration_ = load_calibration(
            cfg.get_string("calibration.table",
                           "resources/calibration/msugs_table.json"),
            &calibration_error);
        if (!calibration_.valid) {
            ED_LOG(Warn, "image") << "calibration disabled: " << calibration_error;
        }
    }
    output_dir_ = cfg.get_string("products.output_dir", "");
    if (output_dir_.empty()) {
        output_dir_ = cfg.get_string("storage.root", "data");
    }
    max_output_dimension_ =
        static_cast<int>(cfg.get_int("products.max_output_dimension", 2048));
    queue_depth_ = static_cast<int>(cfg.get_int("products.queue_depth", 64));
    if (queue_depth_ < 1) {
        queue_depth_ = 1;
    }
    projection_type_ = cfg.get_string("projection.type", "mercator");
    projection_width_ =
        static_cast<int>(cfg.get_int("projection.output_width", 2048));
    projection_height_ =
        static_cast<int>(cfg.get_int("projection.output_height", 2048));
    webp_quality_ = static_cast<int>(cfg.get_int("products.webp_quality", 85));
    webp_method_ = static_cast<int>(cfg.get_int("products.webp_method", 4));
    if (!store_.configure(cfg, error)) {
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void ProductBuilder::submit(const xrit::XritFile& file) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(queue_.size()) >= queue_depth_) {
            products_failed_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Item item;
        item.flush = false;
        item.file = file;
        queue_.push(std::move(item));
    }
    cv_.notify_one();
}

void ProductBuilder::flush() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Item item;
        item.flush = true;
        queue_.push(std::move(item));
    }
    cv_.notify_one();
}

void ProductBuilder::start() {
    if (running_) {
        return;
    }
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { worker_loop(); });
}

void ProductBuilder::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_ = false;
}

ProductBuilder::Stats ProductBuilder::stats() const {
    Stats snapshot;
    snapshot.channels_assembled =
        channels_assembled_.load(std::memory_order_relaxed);
    snapshot.products_written =
        products_written_.load(std::memory_order_relaxed);
    snapshot.products_failed = products_failed_.load(std::memory_order_relaxed);
    snapshot.segments_missing =
        segments_missing_.load(std::memory_order_relaxed);
    return snapshot;
}

void ProductBuilder::set_product_callback(ProductCallback cb) {
    callback_ = std::move(cb);
}

void ProductBuilder::worker_loop() {
    thread::set_name("image-worker");
    thread::set_idle_priority();
    while (true) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_) {
                    break;
                }
                continue;
            }
            item = std::move(queue_.front());
            queue_.pop();
        }
        if (item.flush) {
            std::vector<AssembledChannel> channels;
            assembler_->flush(channels);
            for (AssembledChannel& channel : channels) {
                handle_channel(std::move(channel));
            }
            build_products();
        } else {
            AssembledChannel channel;
            if (assembler_->push(item.file, channel)) {
                handle_channel(std::move(channel));
            }
        }
    }
    std::vector<AssembledChannel> channels;
    assembler_->flush(channels);
    for (AssembledChannel& channel : channels) {
        handle_channel(std::move(channel));
    }
    build_products();
}

void ProductBuilder::handle_channel(AssembledChannel&& channel) {
    const int missing = channel.segments_expected - channel.segments_present;
    if (missing > 0) {
        segments_missing_.fetch_add(static_cast<uint64_t>(missing),
                                    std::memory_order_relaxed);
    }
    pending_[channel.channel_id] = std::move(channel);
    channels_assembled_.fetch_add(1, std::memory_order_relaxed);
}

std::string ProductBuilder::choose_ext() const {
    const std::string requested =
        config_ != nullptr ? config_->get_string("products.format", "webp")
                           : "webp";
#ifdef ED_HAVE_WEBP
    if (requested == "webp") {
        return "webp";
    }
#endif
#ifdef ED_HAVE_PNG
    if (requested == "png" || requested == "webp") {
        return "png";
    }
#endif
    return "ppm";
}

bool ProductBuilder::encode_rgb(const Image8& image, const std::string& ext,
                                std::string& bytes) const {
    if (ext == "webp") {
        output::WebpOptions options;
        options.quality = webp_quality_;
        options.method = webp_method_;
        return output::encode_webp_rgb(image, options, bytes);
    }
    if (ext == "png") {
        return output::encode_png_rgb(image, bytes);
    }
    return output::encode_ppm_rgb(image, bytes);
}

bool ProductBuilder::encode_rgba(const ImageRGBA& image, const std::string& ext,
                                 std::string& bytes) const {
    if (ext == "webp") {
        output::WebpOptions options;
        options.quality = webp_quality_;
        options.method = webp_method_;
        return output::encode_webp_rgba(image, options, bytes);
    }
    if (ext == "png") {
        return output::encode_png_rgba(image, bytes);
    }
    return output::encode_ppm_rgba(image, bytes);
}

void ProductBuilder::build_products() {
    if (pending_.empty() || config_ == nullptr) {
        return;
    }
    int max_channel = 0;
    for (const auto& entry : pending_) {
        max_channel = std::max(max_channel, entry.first);
    }
    std::vector<Image16> channels(static_cast<std::size_t>(max_channel) + 1);
    bool any_partial = false;
    std::string capture_start;
    std::string capture_end;
    const bool calibrated = calibration_enabled_ && calibration_.valid;

    for (auto& entry : pending_) {
        AssembledChannel& channel = entry.second;
        Image16 image;
        if (calibrated) {
            Mask out_valid;
            image = calibrate(channel.image, channel.valid, calibration_,
                              channel.channel_id, &out_valid);
        } else {
            image = channel.image;
        }
        channels[static_cast<std::size_t>(channel.channel_id)] =
            downscale(image, max_output_dimension_);
        if (channel.partial) {
            any_partial = true;
        }
        if (!channel.timestamp_utc.empty()) {
            if (capture_start.empty() || channel.timestamp_utc < capture_start) {
                capture_start = channel.timestamp_utc;
            }
            if (channel.timestamp_utc > capture_end) {
                capture_end = channel.timestamp_utc;
            }
        }
    }

    CompositeConfig visual;
    visual.name = "visual";
    visual.r_channel =
        static_cast<int>(config_->get_int("image.channel_map.vis_r", 0));
    visual.g_channel =
        static_cast<int>(config_->get_int("image.channel_map.vis_g", 1));
    visual.b_channel =
        static_cast<int>(config_->get_int("image.channel_map.vis_b", 2));
    visual.r_min = config_->get_double("products.visual.r_min", 0.0);
    visual.r_max = config_->get_double("products.visual.r_max", 1.0);
    visual.g_min = config_->get_double("products.visual.g_min", 0.0);
    visual.g_max = config_->get_double("products.visual.g_max", 1.0);
    visual.b_min = config_->get_double("products.visual.b_min", 0.0);
    visual.b_max = config_->get_double("products.visual.b_max", 1.0);
    visual.gamma = config_->get_double("products.visual.gamma", 1.0);
    visual.calibrated = calibrated;

    CompositeConfig ir;
    ir.name = "ir";
    ir.ir_channel =
        static_cast<int>(config_->get_int("products.ir.channel", 3));
    ir.ir_min_k = config_->get_double("products.ir.min_k", 190.0);
    ir.ir_max_k = config_->get_double("products.ir.max_k", 320.0);
    ir.lut_path = config_->get_string("products.ir.lut", "");
    ir.calibrated = calibrated;

    CompositeConfig cloud;
    cloud.name = "ir_cloud";
    cloud.ir_channel =
        static_cast<int>(config_->get_int("products.ir_cloud.channel", 3));
    cloud.ir_min_k = ir.ir_min_k;
    cloud.ir_max_k = ir.ir_max_k;
    cloud.cloud_threshold_k =
        config_->get_double("products.ir_cloud.cloud_threshold_k", 250.0);
    cloud.cloud_softness_k =
        config_->get_double("products.ir_cloud.cloud_softness_k", 10.0);
    cloud.lut_path = config_->get_string("products.ir_cloud.lut", "");
    cloud.map_path = config_->get_string("products.ir_cloud.map", "");
    cloud.calibrated = calibrated;

    // GeoTransform from the first channel with navigation, else a default that
    // assumes the full image spans the Earth's angular diameter.
    projection::GeoTransform gt;
    bool have_gt = false;
    for (const auto& entry : pending_) {
        const AssembledChannel& channel = entry.second;
        if (channel.has_navigation) {
            gt = projection::geotransform_from_navigation(
                channel.navigation, channel.image.width, channel.image.height,
                config_->get_double("satellite.subsatellite_lon_deg", 76.0));
            have_gt = true;
            break;
        }
    }
    if (!have_gt) {
        const AssembledChannel& first = pending_.begin()->second;
        gt = projection::default_geotransform(
            first.image.width, first.image.height,
            config_->get_double("satellite.subsatellite_lon_deg", 76.0));
    }

    const std::string stamp = time::compact_utc(time::now_utc());
    const std::string satellite =
        config_->get_string("satellite.id", "elektro-l3");
    const std::string created = time::iso8601_now(false);
    const std::string ext = choose_ext();

    const auto save_product = [&](const std::string& name, int width,
                                  int height,
                                  const projection::GeoTransform& scaled_gt,
                                  const std::string& bytes) {
        output::ProductMeta meta;
        meta.id = stamp + "_" + name;
        meta.satellite = satellite;
        meta.product = name;
        meta.projection = projection_type_;
        meta.width = width;
        meta.height = height;
        meta.capture_start = capture_start;
        meta.capture_end = capture_end;
        meta.created = created;
        meta.partial = any_partial;
        meta.ext = ext;
        if (projection_type_ != "none") {
            double lon_min = 0.0;
            double lon_max = 0.0;
            double lat_min = 0.0;
            double lat_max = 0.0;
            projection::source_bounds(scaled_gt, lon_min, lon_max, lat_min,
                                      lat_max);
            meta.geo.valid = true;
            meta.geo.lon0_deg = scaled_gt.lon0_deg;
            meta.geo.altitude_m = scaled_gt.altitude_m;
            meta.geo.lon_min = lon_min;
            meta.geo.lon_max = lon_max;
            meta.geo.lat_min = lat_min;
            meta.geo.lat_max = lat_max;
        }
        meta.path = store_.relative_path(meta);
        if (!store_.save(meta, bytes)) {
            products_failed_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        products_written_.fetch_add(1, std::memory_order_relaxed);
        if (callback_) {
            callback_(meta);
        }
    };

    const auto project_rgb = [&](const Image8& image,
                                 const projection::GeoTransform& scaled_gt) {
        if (projection_type_ == "mercator") {
            return projector_.reproject_mercator(image, scaled_gt,
                                                 projection_width_,
                                                 projection_height_);
        }
        if (projection_type_ == "equirect") {
            return projector_.reproject_equirect(image, scaled_gt,
                                                 projection_width_,
                                                 projection_height_);
        }
        return image;
    };

    const auto project_rgba = [&](const ImageRGBA& image,
                                  const projection::GeoTransform& scaled_gt) {
        if (projection_type_ == "mercator") {
            return projector_.reproject_mercator_rgba(image, scaled_gt,
                                                      projection_width_,
                                                      projection_height_);
        }
        if (projection_type_ == "equirect") {
            return projector_.reproject_equirect_rgba(image, scaled_gt,
                                                      projection_width_,
                                                      projection_height_);
        }
        return image;
    };

    const Image8 visual_image = build_visual(channels, visual);
    if (!visual_image.empty()) {
        const projection::GeoTransform vgt =
            scaled_geotransform(gt, visual_image.width, visual_image.height);
        const Image8 projected = project_rgb(visual_image, vgt);
        std::string bytes;
        if (encode_rgb(projected, ext, bytes)) {
            save_product("visual", projected.width, projected.height, vgt,
                         bytes);
        } else {
            products_failed_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const Image8 ir_image = build_ir(channels, ir);
    if (!ir_image.empty()) {
        const projection::GeoTransform igt =
            scaled_geotransform(gt, ir_image.width, ir_image.height);
        const Image8 projected = project_rgb(ir_image, igt);
        std::string bytes;
        if (encode_rgb(projected, ext, bytes)) {
            save_product("ir", projected.width, projected.height, igt, bytes);
        } else {
            products_failed_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const ImageRGBA cloud_image = build_ir_cloud(channels, cloud);
    if (!cloud_image.empty()) {
        const projection::GeoTransform cgt =
            scaled_geotransform(gt, cloud_image.width, cloud_image.height);
        const ImageRGBA projected = project_rgba(cloud_image, cgt);
        std::string bytes;
        if (encode_rgba(projected, ext, bytes)) {
            save_product("ir_cloud", projected.width, projected.height, cgt,
                         bytes);
        } else {
            products_failed_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (config_->get_bool("products.ir_cloud.map_enabled", false)) {
#ifdef ED_HAVE_PNG
        const Image8 map = load_png_rgb(cloud.map_path);
        if (!map.empty()) {
            const Image8 mapped = build_ir_cloud_map(channels, cloud, map);
            if (!mapped.empty()) {
                const projection::GeoTransform mgt =
                    scaled_geotransform(gt, mapped.width, mapped.height);
                const Image8 projected = project_rgb(mapped, mgt);
                std::string bytes;
                if (encode_rgb(projected, ext, bytes)) {
                    save_product("ir_cloud_map", projected.width,
                                 projected.height, mgt, bytes);
                } else {
                    products_failed_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
#endif
    }

    pending_.clear();
}

}  // namespace ed::image
