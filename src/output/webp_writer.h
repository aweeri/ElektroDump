// ElektroDump — WebP/PNG/PPM encoder (spec §2.4).
#pragma once

#include <string>

#include "image/image.h"

namespace ed::output {

struct WebpOptions {
    int quality = 85;
    int method = 4;
    bool lossless = false;
};

// Encode to `path` atomically (tmp + rename); falls back to PNG/PPM without libwebp.
bool write_webp_rgb(const image::Image8& image, const std::string& path,
                    const WebpOptions& options);
bool write_webp_rgba(const image::ImageRGBA& image, const std::string& path,
                     const WebpOptions& options);

// Fallbacks used when ED_HAVE_WEBP is undefined.
bool write_png_rgb(const image::Image8& image, const std::string& path);
bool write_png_rgba(const image::ImageRGBA& image, const std::string& path);
bool write_ppm_rgb(const image::Image8& image, const std::string& path);

// Encode to memory (used by the product store).
bool encode_webp_rgb(const image::Image8& image, const WebpOptions& options,
                     std::string& out);
bool encode_webp_rgba(const image::ImageRGBA& image,
                      const WebpOptions& options, std::string& out);
bool encode_png_rgb(const image::Image8& image, std::string& out);
bool encode_png_rgba(const image::ImageRGBA& image, std::string& out);
bool encode_ppm_rgb(const image::Image8& image, std::string& out);
bool encode_ppm_rgba(const image::ImageRGBA& image, std::string& out);

// The format write_webp_* produces in this build: "webp", "png" or "ppm".
const char* webp_format();

}  // namespace ed::output
