#include "output/webp_writer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef ED_HAVE_WEBP
#include <webp/encode.h>
#endif

#ifdef ED_HAVE_PNG
#include <png.h>
#endif

#include "util/fs_util.h"

namespace ed::output {

namespace {

bool encode_ppm(const image::Image8& image, std::string& out) {
    if (image.empty()) {
        return false;
    }
    out = "P6\n" + std::to_string(image.width) + " " +
          std::to_string(image.height) + "\n255\n";
    out.append(reinterpret_cast<const char*>(image.data.data()),
               image.data.size());
    return true;
}

bool encode_ppm_rgba_impl(const image::ImageRGBA& image, std::string& out) {
    if (image.empty()) {
        return false;
    }
    out = "P6\n" + std::to_string(image.width) + " " +
          std::to_string(image.height) + "\n255\n";
    out.reserve(out.size() + static_cast<std::size_t>(image.width) *
                                 static_cast<std::size_t>(image.height) * 3);
    const std::size_t pixels = static_cast<std::size_t>(image.width) *
                               static_cast<std::size_t>(image.height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const uint32_t alpha = image.data[i * 4 + 3];
        for (int c = 0; c < 3; ++c) {
            const uint32_t value =
                image.data[i * 4 + static_cast<std::size_t>(c)];
            out.push_back(static_cast<char>((value * alpha) / 255));
        }
    }
    return true;
}

#ifdef ED_HAVE_WEBP
bool encode_webp(const uint8_t* pixels, int width, int height, int stride,
                 bool has_alpha, const WebpOptions& options,
                 std::string& out) {
    WebPConfig config;
    if (WebPConfigInit(&config) == 0) {
        return false;
    }
    config.quality = static_cast<float>(options.quality);
    config.method = options.method;
    config.lossless = options.lossless ? 1 : 0;
    WebPPicture picture;
    if (WebPPictureInit(&picture) == 0) {
        return false;
    }
    picture.width = width;
    picture.height = height;
    picture.use_argb = 1;
    const int imported = has_alpha
                             ? WebPPictureImportRGBA(&picture, pixels, stride)
                             : WebPPictureImportRGB(&picture, pixels, stride);
    if (imported == 0) {
        WebPPictureFree(&picture);
        return false;
    }
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    const bool ok = WebPEncode(&config, &picture) != 0;
    if (ok) {
        out.assign(reinterpret_cast<const char*>(writer.mem), writer.size);
    }
    WebPMemoryWriterClear(&writer);
    WebPPictureFree(&picture);
    return ok;
}
#endif

}  // namespace

const char* webp_format() {
#ifdef ED_HAVE_WEBP
    return "webp";
#elif defined(ED_HAVE_PNG)
    return "png";
#else
    return "ppm";
#endif
}

bool encode_png_rgb(const image::Image8& image, std::string& out) {
#ifdef ED_HAVE_PNG
    if (image.empty()) {
        return false;
    }
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = static_cast<png_uint_32>(image.width);
    png.height = static_cast<png_uint_32>(image.height);
    png.format = PNG_FORMAT_RGB;
    png_alloc_size_t size = PNG_IMAGE_SIZE(png);
    std::vector<uint8_t> buffer(size);
    if (png_image_write_to_memory(&png, buffer.data(), &size, 0,
                                  image.data.data(), 0, nullptr) == 0) {
        png_image_free(&png);
        return false;
    }
    out.assign(reinterpret_cast<const char*>(buffer.data()), size);
    png_image_free(&png);
    return true;
#else
    (void)image;
    (void)out;
    return false;
#endif
}

bool encode_png_rgba(const image::ImageRGBA& image, std::string& out) {
#ifdef ED_HAVE_PNG
    if (image.empty()) {
        return false;
    }
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = static_cast<png_uint_32>(image.width);
    png.height = static_cast<png_uint_32>(image.height);
    png.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size = PNG_IMAGE_SIZE(png);
    std::vector<uint8_t> buffer(size);
    if (png_image_write_to_memory(&png, buffer.data(), &size, 0,
                                  image.data.data(), 0, nullptr) == 0) {
        png_image_free(&png);
        return false;
    }
    out.assign(reinterpret_cast<const char*>(buffer.data()), size);
    png_image_free(&png);
    return true;
#else
    (void)image;
    (void)out;
    return false;
#endif
}

bool encode_ppm_rgb(const image::Image8& image, std::string& out) {
    return encode_ppm(image, out);
}

bool encode_ppm_rgba(const image::ImageRGBA& image, std::string& out) {
    return encode_ppm_rgba_impl(image, out);
}

bool encode_webp_rgb(const image::Image8& image, const WebpOptions& options,
                     std::string& out) {
#ifdef ED_HAVE_WEBP
    if (image.empty()) {
        return false;
    }
    return encode_webp(image.data.data(), image.width, image.height,
                       image.width * 3, false, options, out);
#else
    (void)options;
    return encode_png_rgb(image, out) || encode_ppm(image, out);
#endif
}

bool encode_webp_rgba(const image::ImageRGBA& image,
                      const WebpOptions& options, std::string& out) {
#ifdef ED_HAVE_WEBP
    if (image.empty()) {
        return false;
    }
    return encode_webp(image.data.data(), image.width, image.height,
                       image.width * 4, true, options, out);
#else
    (void)options;
    return encode_png_rgba(image, out) || encode_ppm_rgba_impl(image, out);
#endif
}

bool write_png_rgb(const image::Image8& image, const std::string& path) {
    std::string bytes;
    if (!encode_png_rgb(image, bytes)) {
        return false;
    }
    return fs::atomic_write(path, bytes);
}

bool write_png_rgba(const image::ImageRGBA& image, const std::string& path) {
    std::string bytes;
    if (!encode_png_rgba(image, bytes)) {
        return false;
    }
    return fs::atomic_write(path, bytes);
}

bool write_ppm_rgb(const image::Image8& image, const std::string& path) {
    std::string bytes;
    if (!encode_ppm(image, bytes)) {
        return false;
    }
    return fs::atomic_write(path, bytes);
}

bool write_webp_rgb(const image::Image8& image, const std::string& path,
                    const WebpOptions& options) {
    std::string bytes;
    if (!encode_webp_rgb(image, options, bytes)) {
        return false;
    }
    return fs::atomic_write(path, bytes);
}

bool write_webp_rgba(const image::ImageRGBA& image, const std::string& path,
                     const WebpOptions& options) {
    std::string bytes;
    if (!encode_webp_rgba(image, options, bytes)) {
        return false;
    }
    return fs::atomic_write(path, bytes);
}

}  // namespace ed::output
