// ElektroDump — plain image buffers (PLAN.md D5, spec §0.1).
// Row-major, channel-interleaved storage; no external library owns the pixels,
// so the projection/WebP layers operate on raw buffers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ed::image {

struct Image16 {  // raw or calibrated 16-bit channel data
    int width = 0;
    int height = 0;
    int channels = 1;
    std::vector<uint16_t> data;  // size = width*height*channels

    bool empty() const { return data.empty(); }

    void resize(int w, int h, int c) {
        width = w;
        height = h;
        channels = c;
        data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                        static_cast<std::size_t>(c),
                    0);
    }
};

struct Image8 {  // 8-bit RGB (3 channels)
    int width = 0;
    int height = 0;
    int channels = 3;
    std::vector<uint8_t> data;

    bool empty() const { return data.empty(); }

    void resize(int w, int h, int c = 3) {
        width = w;
        height = h;
        channels = c;
        data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                        static_cast<std::size_t>(c),
                    0);
    }
};

struct ImageRGBA {  // 8-bit RGBA (4 bytes/pixel)
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;

    bool empty() const { return data.empty(); }

    void resize(int w, int h) {
        width = w;
        height = h;
        data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4,
                    0);
    }
};

struct Mask {  // per-pixel validity (0 = invalid)
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;

    bool empty() const { return data.empty(); }

    void resize(int w, int h) {
        width = w;
        height = h;
        data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    }
};

}  // namespace ed::image
