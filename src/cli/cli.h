// ElektroDump — hand-rolled CLI parser and command table.
#pragma once

#include <string>
#include <vector>

namespace ed::cli {

struct Options {
    std::string config_path = "data/config.json";
    std::string mode;  // --mode ggak|lrit|hrit (overrides config run.mode)
    int verbosity = 0;
    bool quiet = false;
    bool log_json = false;
    bool help = false;
    bool version = false;
};

using Args = std::vector<std::string>;

int run(int argc, char** argv);

void print_help();
void print_command_help(const std::string& command);
void print_version();

// Each command lives in its own translation unit.
int cmd_run(const Options& opts, const Args& args);
int cmd_serve(const Options& opts, const Args& args);
int cmd_decode(const Options& opts, const Args& args);
int cmd_image(const Options& opts, const Args& args);
int cmd_schedule(const Options& opts, const Args& args);
int cmd_probe(const Options& opts, const Args& args);
int cmd_config(const Options& opts, const Args& args);
int cmd_ggak(const Options& opts, const Args& args);

}  // namespace ed::cli
