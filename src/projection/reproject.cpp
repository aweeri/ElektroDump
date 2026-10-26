#include "projection/reproject.h"

#include <cstddef>

namespace ed::projection {

namespace {

bool in_bounds(int width, int height, double sx, double sy) {
    return sx >= 0.0 && sy >= 0.0 &&
           sx <= static_cast<double>(width - 1) &&
           sy <= static_cast<double>(height - 1);
}

void sample_rgb(const image::Image8& src, double sx, double sy, uint8_t* out) {
    const int x0 = static_cast<int>(sx);
    const int y0 = static_cast<int>(sy);
    const int x1 = x0 + 1 < src.width ? x0 + 1 : x0;
    const int y1 = y0 + 1 < src.height ? y0 + 1 : y0;
    const double fx = sx - static_cast<double>(x0);
    const double fy = sy - static_cast<double>(y0);
    for (int c = 0; c < 3; ++c) {
        const double v00 = src.data[(static_cast<std::size_t>(y0) * src.width + x0) * 3 + c];
        const double v10 = src.data[(static_cast<std::size_t>(y0) * src.width + x1) * 3 + c];
        const double v01 = src.data[(static_cast<std::size_t>(y1) * src.width + x0) * 3 + c];
        const double v11 = src.data[(static_cast<std::size_t>(y1) * src.width + x1) * 3 + c];
        const double v = v00 * (1.0 - fx) * (1.0 - fy) +
                         v10 * fx * (1.0 - fy) +
                         v01 * (1.0 - fx) * fy + v11 * fx * fy;
        out[c] = static_cast<uint8_t>(v + 0.5);
    }
}

void sample_rgba(const image::ImageRGBA& src, double sx, double sy,
                 uint8_t* out) {
    const int x0 = static_cast<int>(sx);
    const int y0 = static_cast<int>(sy);
    const int x1 = x0 + 1 < src.width ? x0 + 1 : x0;
    const int y1 = y0 + 1 < src.height ? y0 + 1 : y0;
    const double fx = sx - static_cast<double>(x0);
    const double fy = sy - static_cast<double>(y0);
    for (int c = 0; c < 3; ++c) {
        const double v00 = src.data[(static_cast<std::size_t>(y0) * src.width + x0) * 4 + c];
        const double v10 = src.data[(static_cast<std::size_t>(y0) * src.width + x1) * 4 + c];
        const double v01 = src.data[(static_cast<std::size_t>(y1) * src.width + x0) * 4 + c];
        const double v11 = src.data[(static_cast<std::size_t>(y1) * src.width + x1) * 4 + c];
        const double v = v00 * (1.0 - fx) * (1.0 - fy) +
                         v10 * fx * (1.0 - fy) +
                         v01 * (1.0 - fx) * fy + v11 * fx * fy;
        out[c] = static_cast<uint8_t>(v + 0.5);
    }
    // Nearest-neighbour alpha keeps the validity mask crisp.
    const int nx = static_cast<int>(sx + 0.5);
    const int ny = static_cast<int>(sy + 0.5);
    const int cx = nx < src.width ? nx : src.width - 1;
    const int cy = ny < src.height ? ny : src.height - 1;
    out[3] = src.data[(static_cast<std::size_t>(cy) * src.width + cx) * 4 + 3];
}

}  // namespace

image::Image8 resample_rgb(const image::Image8& src, int out_w, int out_h,
                           const PixelMap& map) {
    image::Image8 out;
    if (src.empty() || out_w <= 0 || out_h <= 0) {
        return out;
    }
    out.resize(out_w, out_h, 3);
    for (int oy = 0; oy < out_h; ++oy) {
        for (int ox = 0; ox < out_w; ++ox) {
            double sx = 0.0;
            double sy = 0.0;
            uint8_t* pixel =
                &out.data[(static_cast<std::size_t>(oy) * out_w + ox) * 3];
            if (!map(ox, oy, sx, sy) ||
                !in_bounds(src.width, src.height, sx, sy)) {
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 0;
                continue;
            }
            sample_rgb(src, sx, sy, pixel);
        }
    }
    return out;
}

image::ImageRGBA resample_rgba(const image::ImageRGBA& src, int out_w,
                               int out_h, const PixelMap& map) {
    image::ImageRGBA out;
    if (src.empty() || out_w <= 0 || out_h <= 0) {
        return out;
    }
    out.resize(out_w, out_h);
    for (int oy = 0; oy < out_h; ++oy) {
        for (int ox = 0; ox < out_w; ++ox) {
            double sx = 0.0;
            double sy = 0.0;
            uint8_t* pixel =
                &out.data[(static_cast<std::size_t>(oy) * out_w + ox) * 4];
            if (!map(ox, oy, sx, sy) ||
                !in_bounds(src.width, src.height, sx, sy)) {
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 0;
                pixel[3] = 0;
                continue;
            }
            sample_rgba(src, sx, sy, pixel);
        }
    }
    return out;
}

}  // namespace ed::projection
