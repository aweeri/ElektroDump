// ElektroDump — small image operations (spec §1.2).
#pragma once

#include "image/image.h"

namespace ed::image {

// Bilinear resample. Returns an empty image on invalid input.
Image16 resize_bilinear(const Image16& src, int width, int height);
Image8 resize_bilinear(const Image8& src, int width, int height);

// Copy a rectangular region; the region is clamped to the source bounds.
Image16 crop(const Image16& src, int x, int y, int width, int height);

// Downscale so the longest side is at most `max_dimension` (no-op if smaller).
Image16 downscale(const Image16& src, int max_dimension);

// Map [min_value, max_value] to 0..255 with optional gamma, into a 1-channel Image8.
void stretch(const Image16& src, double min_value, double max_value,
             double gamma, Image8& out);

// In-place histogram equalisation over the 16-bit value range.
void equalize(Image16& image);

}  // namespace ed::image
