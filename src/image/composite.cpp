#include "image/composite.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "image/alpha_blend.h"
#include "image/image_ops.h"
#include "image/lut.h"

namespace ed::image {

namespace {

double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double normalize(double value, double min_value, double max_value,
                 double gamma) {
    const double range = max_value - min_value;
    double t = range != 0.0 ? (value - min_value) / range : 0.0;
    t = clamp01(t);
    if (gamma > 0.0 && gamma != 1.0) {
        t = std::pow(t, gamma);
    }
    return t;
}

const Image16* channel_at(const std::vector<Image16>& channels, int index) {
    if (index < 0 || index >= static_cast<int>(channels.size())) {
        return nullptr;
    }
    const Image16& channel = channels[static_cast<std::size_t>(index)];
    return channel.empty() ? nullptr : &channel;
}

void dimensions(const std::vector<Image16>& channels, int& width, int& height) {
    width = 0;
    height = 0;
    for (const Image16& channel : channels) {
        if (!channel.empty()) {
            width = channel.width;
            height = channel.height;
            return;
        }
    }
}

uint8_t to_byte(double value) {
    if (value < 0.0) {
        value = 0.0;
    } else if (value > 255.0) {
        value = 255.0;
    }
    return static_cast<uint8_t>(value + 0.5);
}

}  // namespace

Image8 build_visual(const std::vector<Image16>& channels,
                    const CompositeConfig& config) {
    Image8 out;
    int width = 0;
    int height = 0;
    dimensions(channels, width, height);
    if (width <= 0 || height <= 0) {
        return out;
    }
    out.resize(width, height, 3);
    const Image16* red = channel_at(channels, config.r_channel);
    const Image16* green = channel_at(channels, config.g_channel);
    const Image16* blue = channel_at(channels, config.b_channel);
    const double scale = config.calibrated ? 65535.0 : 1023.0;
    const std::size_t pixels =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const double rv = red != nullptr ? red->data[i] / scale : 0.0;
        const double gv = green != nullptr ? green->data[i] / scale : 0.0;
        const double bv = blue != nullptr ? blue->data[i] / scale : 0.0;
        const double rt = normalize(rv, config.r_min, config.r_max, config.gamma);
        const double gt = normalize(gv, config.g_min, config.g_max, config.gamma);
        const double bt = normalize(bv, config.b_min, config.b_max, config.gamma);
        out.data[i * 3] = to_byte(rt * 255.0);
        out.data[i * 3 + 1] = to_byte(gt * 255.0);
        out.data[i * 3 + 2] = to_byte(bt * 255.0);
    }
    return out;
}

Image8 build_ir(const std::vector<Image16>& channels,
                const CompositeConfig& config) {
    Image8 out;
    const Image16* ir = channel_at(channels, config.ir_channel);
    if (ir == nullptr) {
        return out;
    }
    out.resize(ir->width, ir->height, 3);
    Lut1D lut;
    lut.load(config.lut_path);
    const std::size_t pixels =
        static_cast<std::size_t>(ir->width) * static_cast<std::size_t>(ir->height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const double value = static_cast<double>(ir->data[i]);
        const double t = config.calibrated
                             ? normalize(value, config.ir_min_k,
                                         config.ir_max_k, 1.0)
                             : normalize(value, 0.0, 1023.0, 1.0);
        uint8_t rgb[3] = {0, 0, 0};
        lut.sample(t, rgb);
        out.data[i * 3] = rgb[0];
        out.data[i * 3 + 1] = rgb[1];
        out.data[i * 3 + 2] = rgb[2];
    }
    return out;
}

ImageRGBA build_ir_cloud(const std::vector<Image16>& channels,
                         const CompositeConfig& config) {
    ImageRGBA out;
    const Image16* ir = channel_at(channels, config.ir_channel);
    if (ir == nullptr) {
        return out;
    }
    out.resize(ir->width, ir->height);
    Lut1D lut;
    lut.load(config.lut_path);
    const double softness =
        config.cloud_softness_k > 0.0 ? config.cloud_softness_k : 1.0;
    const std::size_t pixels =
        static_cast<std::size_t>(ir->width) * static_cast<std::size_t>(ir->height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const double value = static_cast<double>(ir->data[i]);
        const double temperature =
            config.calibrated
                ? value
                : config.ir_min_k +
                      (value / 1023.0) * (config.ir_max_k - config.ir_min_k);
        // Raw/calibrated zero marks an invalid (off-disk) pixel.
        const double alpha =
            ir->data[i] == 0
                ? 0.0
                : clamp01((config.cloud_threshold_k - temperature) / softness);
        const double t = normalize(temperature, config.ir_min_k,
                                   config.ir_max_k, 1.0);
        uint8_t rgb[3] = {0, 0, 0};
        lut.sample(t, rgb);
        out.data[i * 4] = rgb[0];
        out.data[i * 4 + 1] = rgb[1];
        out.data[i * 4 + 2] = rgb[2];
        out.data[i * 4 + 3] = to_byte(alpha * 255.0);
    }
    return out;
}

Image8 build_ir_cloud_map(const std::vector<Image16>& channels,
                          const CompositeConfig& config, const Image8& map) {
    Image8 out;
    const ImageRGBA cloud = build_ir_cloud(channels, config);
    if (cloud.empty()) {
        return out;
    }
    Image8 resized = map;
    if (map.width != cloud.width || map.height != cloud.height) {
        resized = resize_bilinear(map, cloud.width, cloud.height);
    }
    ImageRGBA bottom;
    bottom.resize(cloud.width, cloud.height);
    const std::size_t pixels =
        static_cast<std::size_t>(cloud.width) * static_cast<std::size_t>(cloud.height);
    for (std::size_t i = 0; i < pixels; ++i) {
        bottom.data[i * 4] = resized.data[i * 3];
        bottom.data[i * 4 + 1] = resized.data[i * 3 + 1];
        bottom.data[i * 4 + 2] = resized.data[i * 3 + 2];
        bottom.data[i * 4 + 3] = 255;
    }
    ImageRGBA blended;
    over(cloud, bottom, blended);
    out.resize(cloud.width, cloud.height, 3);
    for (std::size_t i = 0; i < pixels; ++i) {
        out.data[i * 3] = blended.data[i * 4];
        out.data[i * 3 + 1] = blended.data[i * 4 + 1];
        out.data[i * 3 + 2] = blended.data[i * 4 + 2];
    }
    return out;
}

}  // namespace ed::image
