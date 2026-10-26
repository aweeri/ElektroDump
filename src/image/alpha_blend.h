// ElektroDump — Porter-Duff alpha compositing (spec §1.5).
#pragma once

#include "image/image.h"

namespace ed::image {

// Porter-Duff "over": a_out = a2 + a1*(1-a2);
// c_out = (a2*c2 + a1*c1*(1-a2))/a_out
void over(const ImageRGBA& top, const ImageRGBA& bottom, ImageRGBA& out);

// Per-pixel c = fg*a + (1-a)*bg, alpha kept as max (digest §5.3).
void draw_alpha(const ImageRGBA& fg, Image8& bg);

}  // namespace ed::image
