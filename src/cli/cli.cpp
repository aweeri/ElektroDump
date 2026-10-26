#include "cli/cli.h"

#include <iostream>

#include "core/app.h"
#include "core/logging.h"

#ifndef ED_BUILD_MODE
#define ED_BUILD_MODE "unknown"
#endif

namespace ed::cli {

namespace {

const char* kHelpText =
    "ElektroDump — headless Elektro-L ground station\n"
    "\n"
    "Usage:\n"
    "  elektrodump <command> [options]\n"
    "\n"
    "Commands:\n"
    "  run                     Start the daemon (SDR + scheduler + web + webhook)\n"
    "  serve                   Start only the web server (offline browsing)\n"
    "  decode <dir>            Decode a CADU spool directory to products\n"
    "  image <xrit-dir> [out]  Assemble xRIT files into image products\n"
    "  schedule list           Print the schedule\n"
    "  schedule add ...        Add an entry\n"
    "  schedule remove <name>  Remove an entry\n"
    "  schedule enable|disable Toggle the schedule\n"
    "  probe                   List SDR devices and capabilities\n"
    "  config validate         Validate the effective config\n"
    "  config dump             Print the effective config\n"
    "  ggak decode <dir>       Decode GGAK CADU to CSV/JSON\n"
    "  version                 Print version and build info\n"
    "  help [command]          Show help\n"
    "\n"
    "Common options:\n"
    "  -c, --config <path>     Config file (default: data/config.json)\n"
    "      --mode <mode>       Pipeline mode: ggak | lrit | hrit\n"
    "  -v, --verbose           Increase log level (repeatable)\n"
    "  -q, --quiet             Errors only\n"
    "      --log-json          Structured JSON logs\n"
    "  -h, --help              Show help\n"
    "      --version           Show version\n";

void print_usage_error(const std::string& message) {
    std::cerr << "error: " << message << "\n";
    std::cerr << "Try 'elektrodump help' for usage.\n";
}

}  // namespace

void print_version() {
    std::cout << kAppName << " " << kVersion << "\n";
    std::cout << "build: " << ED_BUILD_MODE << " (" << __VERSION__ << ")\n";
    std::cout << "compiled: " << __DATE__ << " " << __TIME__ << "\n";
}

void print_help() {
    std::cout << kHelpText;
}

void print_command_help(const std::string& command) {
    if (command == "run") {
        std::cout << "Usage: elektrodump run [options]\n"
                     "Start the daemon: open the SDR, run the FFT/waterfall\n"
                     "producer, the active pipeline (GGAK/LRIT/HRIT, selected by\n"
                     "config run.mode or the time-of-day schedule) and serve the\n"
                     "status API.\n";
    } else if (command == "serve") {
        std::cout << "Usage: elektrodump serve [options]\n"
                     "Start only the web server (status API) for offline\n"
                     "browsing; no SDR is opened.\n";
    } else if (command == "decode") {
        std::cout << "Usage: elektrodump decode <dir> [options]\n"
                     "Decode LRIT/HRIT offline: `.cadu` files (1024-byte CADUs)\n"
                     "are derandomised, Reed-Solomon corrected and demultiplexed;\n"
                     "`.cf32`/`.cu8` IQ files are demodulated first (BPSK for\n"
                     "LRIT, OQPSK + coarse acquisition for HRIT). Select the mode\n"
                     "with --mode hrit or config run.mode. Assembled xRIT files\n"
                     "are written to the mode's output_dir.\n";
    } else if (command == "image") {
        std::cout << "Usage: elektrodump image <xrit-dir> [out-dir] [options]\n"
                     "Assemble the `.lrit` files in a directory into full-disk\n"
                     "channels, calibrate them, reproject to the configured\n"
                     "projection and write the visual, ir and ir_cloud products\n"
                     "(WebP when libwebp is available, else PNG/PPM).\n"
                     "Products default to <xrit-dir>/products.\n";
    } else if (command == "schedule") {
        std::cout << "Usage: elektrodump schedule <list|add|remove|enable|disable> "
                     "[options]\n"
                     "Inspect or edit the capture schedule (persisted to the\n"
                     "user config).\n"
                     "  list                    Print the schedule and timezone\n"
                     "  add --name <n> --start HH:MM --end HH:MM --mode lrit|hrit\n"
                     "      [--priority N] [--frequency HZ] [--symbol-rate R]\n"
                     "  remove <name>           Remove an entry\n"
                     "  enable | disable        Toggle the schedule\n";
    } else if (command == "probe") {
        std::cout << "Usage: elektrodump probe [options]\n"
                     "List SDR devices and capabilities (works without a\n"
                     "config file).\n";
    } else if (command == "config") {
        std::cout << "Usage: elektrodump config <validate|dump> [options]\n"
                     "  validate  Validate the effective config\n"
                     "  dump      Print the effective config\n";
    } else if (command == "ggak") {
        std::cout << "Usage: elektrodump ggak decode <dir> [options]\n"
                     "Decode GGAK CADU (.cadu) or IQ (.cf32/.cu8) files in a\n"
                     "directory and write a ggak.csv summary.\n";
    } else if (command == "version") {
        std::cout << "Usage: elektrodump version\n";
    } else if (command == "help") {
        std::cout << "Usage: elektrodump help [command]\n";
    } else {
        print_help();
    }
}

int run(int argc, char** argv) {
    Options opts;
    Args positional;
    // The `schedule` subcommands have their own flags; once the command is known,
    // unknown options and `--mode` are forwarded to the subcommand.
    bool schedule_cmd = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            opts.help = true;
        } else if (arg == "--version") {
            opts.version = true;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                print_usage_error("missing value for " + arg);
                return 2;
            }
            opts.config_path = argv[++i];
        } else if (arg.rfind("--config=", 0) == 0) {
            opts.config_path = arg.substr(9);
        } else if (arg == "--mode") {
            if (schedule_cmd) {
                positional.push_back(arg);
                if (i + 1 < argc) {
                    positional.push_back(argv[++i]);
                }
            } else {
                if (i + 1 >= argc) {
                    print_usage_error("missing value for " + arg);
                    return 2;
                }
                opts.mode = argv[++i];
            }
        } else if (arg.rfind("--mode=", 0) == 0) {
            if (schedule_cmd) {
                positional.push_back(arg);
            } else {
                opts.mode = arg.substr(7);
            }
        } else if (arg == "-v" || arg == "--verbose") {
            ++opts.verbosity;
        } else if (arg == "-q" || arg == "--quiet") {
            opts.quiet = true;
        } else if (arg == "--log-json") {
            opts.log_json = true;
        } else if (!arg.empty() && arg.front() == '-') {
            if (schedule_cmd) {
                positional.push_back(arg);
            } else {
                print_usage_error("unknown option: " + arg);
                return 2;
            }
        } else {
            if (positional.empty() && arg == "schedule") {
                schedule_cmd = true;
            }
            positional.push_back(arg);
        }
    }

    // Configure the logger from CLI flags; commands may refine it from config.
    if (opts.quiet) {
        Logger::instance().set_level(LogLevel::Error);
    } else if (opts.verbosity >= 2) {
        Logger::instance().set_level(LogLevel::Trace);
    } else if (opts.verbosity == 1) {
        Logger::instance().set_level(LogLevel::Debug);
    }
    Logger::instance().set_json(opts.log_json);

    if (opts.version) {
        print_version();
        return 0;
    }

    if (positional.empty()) {
        print_help();
        return 0;
    }

    const std::string command = positional[0];
    const Args args(positional.begin() + 1, positional.end());

    if (opts.help) {
        print_command_help(command);
        return 0;
    }

    if (command == "run") {
        return cmd_run(opts, args);
    }
    if (command == "serve") {
        return cmd_serve(opts, args);
    }
    if (command == "decode") {
        return cmd_decode(opts, args);
    }
    if (command == "image") {
        return cmd_image(opts, args);
    }
    if (command == "schedule") {
        return cmd_schedule(opts, args);
    }
    if (command == "probe") {
        return cmd_probe(opts, args);
    }
    if (command == "config") {
        return cmd_config(opts, args);
    }
    if (command == "ggak") {
        return cmd_ggak(opts, args);
    }
    if (command == "version") {
        print_version();
        return 0;
    }
    if (command == "help") {
        if (args.empty()) {
            print_help();
        } else {
            print_command_help(args[0]);
        }
        return 0;
    }

    print_usage_error("unknown command: " + command);
    return 2;
}

}  // namespace ed::cli
