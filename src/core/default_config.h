// ElektroDump — access to the compiled-in default configuration.
#pragma once

#include <string>

namespace ed {

// Contents of resources/default_config.json, embedded at build time by the Makefile.
std::string default_config_json();

}  // namespace ed
