#include "image/lut.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#ifdef ED_HAVE_PNG
#include <png.h>
#endif

namespace ed::image {

namespace {

#ifdef ED_HAVE_PNG
bool load_png_rgb(const std::string& path, int& width, int& height,
                  std::vector<uint8_t>& data) {
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str())) {
        return false;
    }
    image.format = PNG_FORMAT_RGB;
    data.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, data.data(), 0, nullptr)) {
        png_image_free(&image);
        return false;
    }
    width = static_cast<int>(image.width);
    height = static_cast<int>(image.height);
    png_image_free(&image);
    return width > 0 && height > 0;
}
#endif

void build_grayscale_ramp(int& width, std::vector<uint8_t>& data) {
    width = 256;
    data.resize(static_cast<std::size_t>(width) * 3);
    for (int i = 0; i < width; ++i) {
        const uint8_t value = static_cast<uint8_t>(i);
        data[static_cast<std::size_t>(i) * 3] = value;
        data[static_cast<std::size_t>(i) * 3 + 1] = value;
        data[static_cast<std::size_t>(i) * 3 + 2] = value;
    }
}

}  // namespace

bool Lut1D::load(const std::string& path) {
#ifdef ED_HAVE_PNG
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
    if (load_png_rgb(path, width, height, data)) {
        // Use the first row of the strip.
        width_ = width;
        data_.assign(data.begin(),
                     data.begin() + static_cast<std::ptrdiff_t>(width) * 3);
        loaded_ = true;
        return true;
    }
#else
    (void)path;
#endif
    // Fallback: built-in grayscale ramp.
    build_grayscale_ramp(width_, data_);
    loaded_ = true;
    return true;
}

void Lut1D::sample(double t, uint8_t rgb[3]) const {
    if (!loaded_ || width_ <= 0) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    const double position = t * static_cast<double>(width_ - 1);
    const int index = static_cast<int>(position);
    const int next = std::min(index + 1, width_ - 1);
    const double fraction = position - static_cast<double>(index);
    for (int c = 0; c < 3; ++c) {
        const double a =
            static_cast<double>(data_[static_cast<std::size_t>(index) * 3 +
                                      static_cast<std::size_t>(c)]);
        const double b =
            static_cast<double>(data_[static_cast<std::size_t>(next) * 3 +
                                      static_cast<std::size_t>(c)]);
        rgb[c] = static_cast<uint8_t>(a + (b - a) * fraction + 0.5);
    }
}

bool Lut2D::load(const std::string& path) {
#ifdef ED_HAVE_PNG
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
    if (!load_png_rgb(path, width, height, data)) {
        return false;
    }
    width_ = width;
    height_ = height;
    data_ = std::move(data);
    loaded_ = true;
    return true;
#else
    (void)path;
    return false;
#endif
}

void Lut2D::sample(double u, double v, uint8_t rgb[3]) const {
    if (!loaded_ || width_ <= 0 || height_ <= 0) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    if (u < 0.0) {
        u = 0.0;
    } else if (u > 1.0) {
        u = 1.0;
    }
    if (v < 0.0) {
        v = 0.0;
    } else if (v > 1.0) {
        v = 1.0;
    }
    const double px = u * static_cast<double>(width_ - 1);
    const double py = v * static_cast<double>(height_ - 1);
    const int x0 = static_cast<int>(px);
    const int y0 = static_cast<int>(py);
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const double fx = px - static_cast<double>(x0);
    const double fy = py - static_cast<double>(y0);
    for (int c = 0; c < 3; ++c) {
        const auto at = [&](int x, int y) {
            return static_cast<double>(
                data_[(static_cast<std::size_t>(y) *
                           static_cast<std::size_t>(width_) +
                       static_cast<std::size_t>(x)) *
                          3 +
                      static_cast<std::size_t>(c)]);
        };
        const double value = at(x0, y0) * (1.0 - fx) * (1.0 - fy) +
                             at(x1, y0) * fx * (1.0 - fy) +
                             at(x0, y1) * (1.0 - fx) * fy +
                             at(x1, y1) * fx * fy;
        rgb[c] = static_cast<uint8_t>(value + 0.5);
    }
}

}  // namespace ed::image
