// ElektroDump — inverse-map resampling (spec §2.3).
#pragma once

#include <functional>

#include "image/image.h"

namespace ed::projection {

// Maps an output pixel (ox, oy) to a source pixel (sx, sy); false when off-disk.
using PixelMap = std::function<bool(int ox, int oy, double& sx, double& sy)>;

// Bilinear resample of an RGB image through `map`.
image::Image8 resample_rgb(const image::Image8& src, int out_w, int out_h,
                           const PixelMap& map);

// Bilinear RGB + nearest alpha resample of an RGBA image through `map`.
image::ImageRGBA resample_rgba(const image::ImageRGBA& src, int out_w,
                               int out_h, const PixelMap& map);

}  // namespace ed::projection
