// ElektroDump — 1D/2D palette sampling (libpng optional, spec §1.5).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ed::image {

class Lut1D {
public:
    bool load(const std::string& path);  // PNG strip, Nx1
    bool loaded() const { return loaded_; }
    void sample(double t, uint8_t rgb[3]) const;  // t in 0..1, clamped

private:
    bool loaded_ = false;
    int width_ = 0;
    std::vector<uint8_t> data_;  // RGB triplets
};

class Lut2D {
public:
    bool load(const std::string& path);  // PNG grid, NxM
    bool loaded() const { return loaded_; }
    void sample(double u, double v, uint8_t rgb[3]) const;  // bilinear

private:
    bool loaded_ = false;
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> data_;  // RGB triplets
};

}  // namespace ed::image
