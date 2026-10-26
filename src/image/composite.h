// ElektroDump — visual / ir / ir_cloud / ir_cloud_map builders (spec §1.6).
#pragma once

#include <string>
#include <vector>

#include "image/image.h"

namespace ed::image {

struct CompositeConfig {
    std::string name;  // "visual" | "ir" | "ir_cloud" | "ir_cloud_map"
    int r_channel = 0;
    int g_channel = 1;
    int b_channel = 2;
    double r_min = 0.0;
    double r_max = 1.0;
    double g_min = 0.0;
    double g_max = 1.0;
    double b_min = 0.0;
    double b_max = 1.0;
    double gamma = 1.0;
    std::string lut_path;  // optional 1D/2D LUT
    int ir_channel = 3;
    double ir_min_k = 190.0;
    double ir_max_k = 320.0;
    double cloud_threshold_k = 250.0;
    double cloud_softness_k = 10.0;
    std::string map_path;  // ir_cloud_map underlay
    // True when input channels are calibrated (reflectance 0..65535, Kelvin);
    // false means raw 0..1023 counts.
    bool calibrated = true;
};

Image8 build_visual(const std::vector<Image16>& channels,
                    const CompositeConfig& config);
Image8 build_ir(const std::vector<Image16>& channels,
                const CompositeConfig& config);
ImageRGBA build_ir_cloud(const std::vector<Image16>& channels,
                         const CompositeConfig& config);
Image8 build_ir_cloud_map(const std::vector<Image16>& channels,
                          const CompositeConfig& config, const Image8& map);

}  // namespace ed::image
