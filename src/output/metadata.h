// ElektroDump — product JSON sidecar (spec §2.5).
#pragma once

#include <string>

#include "output/product_meta.h"

namespace ed::output {

std::string metadata_to_json(const ProductMeta& meta);

bool metadata_from_json(const std::string& text, ProductMeta& meta);

}  // namespace ed::output
