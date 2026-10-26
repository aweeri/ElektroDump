#include <iostream>

#include "cli/cli.h"
#include "core/app.h"

namespace ed::cli {

int cmd_run(const Options& opts, const Args& /*args*/) {
    App app;
    std::string error;
    if (!app.initialize(opts.config_path, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    return app.run();
}

}  // namespace ed::cli
