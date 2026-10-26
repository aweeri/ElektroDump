#include <iostream>
#include <string>
#include <vector>

#include "cli/cli.h"
#include "core/config.h"
#include "core/logging.h"

namespace ed::cli {

int cmd_config(const Options& opts, const Args& args) {
    if (args.empty()) {
        std::cerr << "error: config requires a subcommand (validate|dump)\n";
        return 2;
    }
    const std::string& sub = args[0];
    if (sub != "validate" && sub != "dump") {
        std::cerr << "error: unknown config subcommand: " << sub << "\n";
        return 2;
    }

    std::string error;
    Config config = Config::load(opts.config_path, &error);
    if (!error.empty()) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    LogLevel level = LogLevel::Info;
    if (parse_log_level(config.get_string("logging.level", "info"), level)) {
        Logger::instance().set_level(level);
    }

    if (!config.loaded_from_file()) {
        std::cerr << "warning: config file '" << opts.config_path
                  << "' not found; using embedded defaults\n";
    }

    if (sub == "dump") {
        std::cout << config.dump(2) << "\n";
        return 0;
    }

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    const bool ok = config.validate(&errors, &warnings);
    for (const std::string& warning : warnings) {
        std::cerr << "warning: " << warning << "\n";
    }
    if (!ok) {
        for (const std::string& err : errors) {
            std::cerr << "error: " << err << "\n";
        }
        std::cerr << "config invalid (" << errors.size() << " error(s))\n";
        return 1;
    }
    std::cout << "config OK ("
              << (config.loaded_from_file() ? opts.config_path
                                            : "embedded defaults")
              << ")\n";
    return 0;
}

}  // namespace ed::cli
