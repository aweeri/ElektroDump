#include <iostream>
#include <string>
#include <vector>

#include "cli/cli.h"
#include "core/config.h"
#include "sdr/sdr_source.h"

namespace ed::cli {

namespace {

void print_device(const ed::sdr::SdrInfo& info) {
    std::cout << "  [" << info.driver << "] " << info.name;
    if (!info.serial.empty()) {
        std::cout << "  sn=" << info.serial;
    }
    if (!info.available) {
        std::cout << "  (backend not compiled in)";
    }
    std::cout << "\n";
    std::cout << "      sample rate: " << info.min_sample_rate << ".."
              << info.max_sample_rate << " Hz\n";
    std::cout << "      bias tee:    " << (info.has_bias_tee ? "yes" : "no")
              << "\n";
    if (!info.gains_tenth_db.empty()) {
        std::cout << "      gains (0.1 dB): ";
        for (std::size_t i = 0; i < info.gains_tenth_db.size(); ++i) {
            if (i != 0) {
                std::cout << ", ";
            }
            std::cout << info.gains_tenth_db[i];
        }
        std::cout << "\n";
    }
}

}  // namespace

int cmd_probe(const Options& opts, const Args& /*args*/) {
    // Probe works without a config file; load it only for context.
    std::string error;
    const Config config = Config::load(opts.config_path, &error);
    const std::string configured = config.get_string("sdr.driver", "");

    const std::vector<ed::sdr::SdrInfo> devices = ed::sdr::probe_devices();
    std::cout << "SDR devices (" << devices.size() << "):\n";
    if (devices.empty()) {
        std::cout << "  (none detected)\n";
    }
    for (const ed::sdr::SdrInfo& info : devices) {
        print_device(info);
    }

    std::cout << "\nBackends: ";
    for (const std::string& name : ed::sdr::known_drivers()) {
        std::cout << name << (ed::sdr::backend_available(name) ? "=yes " : "=no ");
    }
    std::cout << "\n";
    if (!configured.empty()) {
        std::cout << "configured driver: " << configured << "\n";
    }
    return 0;
}

}  // namespace ed::cli
