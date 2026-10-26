#include "image/alpha_blend.h"

#include <algorithm>
#include <cstddef>

namespace ed::image {

namespace {

uint8_t to_byte(double value) {
    if (value < 0.0) {
        value = 0.0;
    } else if (value > 255.0) {
        value = 255.0;
    }
    return static_cast<uint8_t>(value + 0.5);
}

}  // namespace

void over(const ImageRGBA& top, const ImageRGBA& bottom, ImageRGBA& out) {
    const int width = std::min(top.width, bottom.width);
    const int height = std::min(top.height, bottom.height);
    out.resize(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) *
                4;
            const double a1 = static_cast<double>(top.data[index + 3]) / 255.0;
            const double a2 =
                static_cast<double>(bottom.data[index + 3]) / 255.0;
            const double a_out = a2 + a1 * (1.0 - a2);
            for (int c = 0; c < 3; ++c) {
                const double c1 = static_cast<double>(top.data[index + c]);
                const double c2 = static_cast<double>(bottom.data[index + c]);
                const double value =
                    a_out > 0.0
                        ? (a2 * c2 + a1 * c1 * (1.0 - a2)) / a_out
                        : 0.0;
                out.data[index + static_cast<std::size_t>(c)] = to_byte(value);
            }
            out.data[index + 3] = to_byte(a_out * 255.0);
        }
    }
}

void draw_alpha(const ImageRGBA& fg, Image8& bg) {
    if (fg.width != bg.width || fg.height != bg.height) {
        return;
    }
    for (int y = 0; y < bg.height; ++y) {
        for (int x = 0; x < bg.width; ++x) {
            const std::size_t fg_index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(bg.width) +
                 static_cast<std::size_t>(x)) *
                4;
            const std::size_t bg_index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(bg.width) +
                 static_cast<std::size_t>(x)) *
                3;
            const double alpha =
                static_cast<double>(fg.data[fg_index + 3]) / 255.0;
            for (int c = 0; c < 3; ++c) {
                const double f = static_cast<double>(fg.data[fg_index + c]);
                const double b = static_cast<double>(bg.data[bg_index + c]);
                bg.data[bg_index + static_cast<std::size_t>(c)] =
                    to_byte(f * alpha + b * (1.0 - alpha));
            }
        }
    }
}

}  // namespace ed::image
