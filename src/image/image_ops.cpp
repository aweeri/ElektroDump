#include "image/image_ops.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ed::image {

namespace {

double sample16(const Image16& src, int x, int y, int c) {
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(src.width) +
         static_cast<std::size_t>(x)) *
            static_cast<std::size_t>(src.channels) +
        static_cast<std::size_t>(c);
    return static_cast<double>(src.data[index]);
}

double sample8(const Image8& src, int x, int y, int c) {
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(src.width) +
         static_cast<std::size_t>(x)) *
            static_cast<std::size_t>(src.channels) +
        static_cast<std::size_t>(c);
    return static_cast<double>(src.data[index]);
}

}  // namespace

Image16 resize_bilinear(const Image16& src, int width, int height) {
    Image16 out;
    if (src.width <= 0 || src.height <= 0 || width <= 0 || height <= 0) {
        return out;
    }
    out.resize(width, height, src.channels);
    const double sx = static_cast<double>(src.width) / width;
    const double sy = static_cast<double>(src.height) / height;
    for (int y = 0; y < height; ++y) {
        double fy = (static_cast<double>(y) + 0.5) * sy - 0.5;
        if (fy < 0.0) {
            fy = 0.0;
        }
        int y0 = static_cast<int>(fy);
        if (y0 > src.height - 1) {
            y0 = src.height - 1;
        }
        const int y1 = std::min(y0 + 1, src.height - 1);
        const double wy = fy - static_cast<double>(y0);
        for (int x = 0; x < width; ++x) {
            double fx = (static_cast<double>(x) + 0.5) * sx - 0.5;
            if (fx < 0.0) {
                fx = 0.0;
            }
            int x0 = static_cast<int>(fx);
            if (x0 > src.width - 1) {
                x0 = src.width - 1;
            }
            const int x1 = std::min(x0 + 1, src.width - 1);
            const double wx = fx - static_cast<double>(x0);
            for (int c = 0; c < src.channels; ++c) {
                const double v = sample16(src, x0, y0, c) * (1.0 - wx) * (1.0 - wy) +
                                 sample16(src, x1, y0, c) * wx * (1.0 - wy) +
                                 sample16(src, x0, y1, c) * (1.0 - wx) * wy +
                                 sample16(src, x1, y1, c) * wx * wy;
                const std::size_t index =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) *
                        static_cast<std::size_t>(src.channels) +
                    static_cast<std::size_t>(c);
                out.data[index] = static_cast<uint16_t>(v + 0.5);
            }
        }
    }
    return out;
}

Image8 resize_bilinear(const Image8& src, int width, int height) {
    Image8 out;
    if (src.width <= 0 || src.height <= 0 || width <= 0 || height <= 0) {
        return out;
    }
    out.resize(width, height, src.channels);
    const double sx = static_cast<double>(src.width) / width;
    const double sy = static_cast<double>(src.height) / height;
    for (int y = 0; y < height; ++y) {
        double fy = (static_cast<double>(y) + 0.5) * sy - 0.5;
        if (fy < 0.0) {
            fy = 0.0;
        }
        int y0 = static_cast<int>(fy);
        if (y0 > src.height - 1) {
            y0 = src.height - 1;
        }
        const int y1 = std::min(y0 + 1, src.height - 1);
        const double wy = fy - static_cast<double>(y0);
        for (int x = 0; x < width; ++x) {
            double fx = (static_cast<double>(x) + 0.5) * sx - 0.5;
            if (fx < 0.0) {
                fx = 0.0;
            }
            int x0 = static_cast<int>(fx);
            if (x0 > src.width - 1) {
                x0 = src.width - 1;
            }
            const int x1 = std::min(x0 + 1, src.width - 1);
            const double wx = fx - static_cast<double>(x0);
            for (int c = 0; c < src.channels; ++c) {
                const double v = sample8(src, x0, y0, c) * (1.0 - wx) * (1.0 - wy) +
                                 sample8(src, x1, y0, c) * wx * (1.0 - wy) +
                                 sample8(src, x0, y1, c) * (1.0 - wx) * wy +
                                 sample8(src, x1, y1, c) * wx * wy;
                const std::size_t index =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) *
                        static_cast<std::size_t>(src.channels) +
                    static_cast<std::size_t>(c);
                out.data[index] = static_cast<uint8_t>(v + 0.5);
            }
        }
    }
    return out;
}

Image16 crop(const Image16& src, int x, int y, int width, int height) {
    Image16 out;
    if (src.width <= 0 || src.height <= 0 || width <= 0 || height <= 0) {
        return out;
    }
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(src.width, x + width);
    const int y1 = std::min(src.height, y + height);
    if (x1 <= x0 || y1 <= y0) {
        return out;
    }
    out.resize(x1 - x0, y1 - y0, src.channels);
    for (int row = 0; row < out.height; ++row) {
        for (int col = 0; col < out.width; ++col) {
            for (int c = 0; c < src.channels; ++c) {
                const std::size_t src_index =
                    (static_cast<std::size_t>(y0 + row) *
                         static_cast<std::size_t>(src.width) +
                     static_cast<std::size_t>(x0 + col)) *
                        static_cast<std::size_t>(src.channels) +
                    static_cast<std::size_t>(c);
                const std::size_t dst_index =
                    (static_cast<std::size_t>(row) *
                         static_cast<std::size_t>(out.width) +
                     static_cast<std::size_t>(col)) *
                        static_cast<std::size_t>(out.channels) +
                    static_cast<std::size_t>(c);
                out.data[dst_index] = src.data[src_index];
            }
        }
    }
    return out;
}

Image16 downscale(const Image16& src, int max_dimension) {
    if (src.width <= 0 || src.height <= 0 || max_dimension <= 0) {
        return src;
    }
    const int longest = std::max(src.width, src.height);
    if (longest <= max_dimension) {
        return src;
    }
    const double scale = static_cast<double>(max_dimension) / longest;
    const int width = std::max(1, static_cast<int>(src.width * scale + 0.5));
    const int height = std::max(1, static_cast<int>(src.height * scale + 0.5));
    return resize_bilinear(src, width, height);
}

void stretch(const Image16& src, double min_value, double max_value,
             double gamma, Image8& out) {
    out.resize(src.width, src.height, 1);
    if (src.empty()) {
        return;
    }
    const double range = max_value - min_value;
    const double inv_range = range != 0.0 ? 1.0 / range : 0.0;
    const double g = gamma > 0.0 ? gamma : 1.0;
    for (std::size_t i = 0; i < src.data.size(); ++i) {
        double t = (static_cast<double>(src.data[i]) - min_value) * inv_range;
        if (t < 0.0) {
            t = 0.0;
        } else if (t > 1.0) {
            t = 1.0;
        }
        if (g != 1.0) {
            t = std::pow(t, g);
        }
        out.data[i] = static_cast<uint8_t>(t * 255.0 + 0.5);
    }
}

void equalize(Image16& image) {
    if (image.empty()) {
        return;
    }
    constexpr std::size_t kBins = 65536;
    std::vector<uint64_t> histogram(kBins, 0);
    for (uint16_t value : image.data) {
        ++histogram[value];
    }
    uint64_t cumulative = 0;
    std::vector<uint16_t> mapping(kBins, 0);
    const uint64_t total = image.data.size();
    for (std::size_t i = 0; i < kBins; ++i) {
        cumulative += histogram[i];
        mapping[i] = static_cast<uint16_t>(
            total > 0 ? (cumulative * 65535ULL) / total : 0);
    }
    for (uint16_t& value : image.data) {
        value = mapping[value];
    }
}

}  // namespace ed::image
