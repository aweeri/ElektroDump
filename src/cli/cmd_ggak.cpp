// ElektroDump — `ggak decode <dir>` (PLAN.md D11, milestone M3).
// Offline GGAK processing. `.cadu` files (already-demodulated 224-byte frames)
// are validated directly; `.cf32`/`.cu8` IQ files run through the same PM demod
// + frame sync as the live pipeline. A `ggak.csv` summary is written to the dir.
#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>

#include "cli/cli.h"
#include "core/config.h"
#include "core/time_util.h"
#include "dsp/demod_pm.h"
#include "ggak/ggak_frame.h"
#include "util/fs_util.h"
#include "util/string_util.h"

namespace ed::cli {

namespace {

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

std::string csv_utc(double unix_time) {
    if (!(unix_time > 0.0)) {
        return "";
    }
    const std::time_t t = static_cast<std::time_t>(unix_time);
    return time::format_utc(t, "%Y-%m-%dT%H:%M:%SZ");
}

struct Summary {
    uint64_t files = 0;
    uint64_t frames = 0;
    uint64_t valid = 0;
    uint64_t invalid = 0;
    double snr_db = 0.0;
};

// Recursively collect regular files under `dir` (the spool nests under YYYYMMDD).
void collect_files(const std::string& dir, std::vector<std::string>& out) {
    DIR* handle = opendir(dir.c_str());
    if (handle == nullptr) {
        return;
    }
    std::vector<std::string> names;
    while (struct dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        names.push_back(name);
    }
    closedir(handle);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const std::string path = fs::join(dir, name);
        if (fs::is_directory(path)) {
            collect_files(path, out);
        } else if (fs::is_regular_file(path)) {
            out.push_back(path);
        }
    }
}

void write_csv_row(std::ofstream& csv, const ggak::GgakFrame& frame) {
    csv << csv_utc(frame.timestamp_unix) << ',' << static_cast<int>(frame.id)
        << ',' << (frame.crc_ok ? 1 : 0) << ',' << frame.frame_time << '\n';
}

void decode_cadu_file(const std::string& path, std::ofstream& csv,
                      Summary& summary) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "warning: cannot open " << path << "\n";
        return;
    }
    ++summary.files;

    uint8_t raw[ggak::kGgakFrameBytes];
    while (in.read(reinterpret_cast<char*>(raw), sizeof(raw))) {
        ggak::GgakFrame frame;
        std::memcpy(frame.raw, raw, sizeof(raw));
        frame.id = raw[4];
        frame.frame_time = (static_cast<uint32_t>(raw[9]) << 24) |
                           (static_cast<uint32_t>(raw[10]) << 16) |
                           (static_cast<uint32_t>(raw[11]) << 8) |
                           static_cast<uint32_t>(raw[12]);
        frame.crc_ok = ggak::ggak_frame_crc_ok(raw);
        frame.timestamp_unix = static_cast<double>(frame.frame_time) +
                               10957.0 * 86400.0 - 3.0 * 3600.0;
        ++summary.frames;
        if (frame.crc_ok) {
            ++summary.valid;
        } else {
            ++summary.invalid;
        }
        write_csv_row(csv, frame);
    }
}

void decode_iq_file(const std::string& path, bool is_cu8,
                    const Config& cfg, std::ofstream& csv, Summary& summary) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "warning: cannot open " << path << "\n";
        return;
    }
    ++summary.files;

    dsp::PmDemodConfig dcfg;
    dcfg.symbol_rate = cfg.get_double("ggak.symbol_rate", 5000.0);
    dcfg.rrc_alpha = cfg.get_double("ggak.rrc_alpha", 0.5);
    dcfg.pll_bw = cfg.get_double("ggak.pll_bw", 0.02);
    dcfg.costas_bw = cfg.get_double("ggak.costas_bw", 0.02);
    dcfg.max_carrier_offset_hz =
        cfg.get_double("ggak.max_carrier_offset_hz", 50000.0);
    dcfg.subcarrier_hz = cfg.get_double("ggak.subcarrier_hz", 0.0);
    dcfg.subcarrier_enabled =
        cfg.get_bool("ggak.subcarrier_enabled", true);
    dcfg.target_sps = static_cast<int>(cfg.get_int("ggak.target_sps", 8));
    dcfg.rrc_taps = static_cast<int>(cfg.get_int("ggak.rrc_taps", 33));

    const uint32_t sample_rate = static_cast<uint32_t>(
        cfg.get_int("ggak.sample_rate_hz", 3000000));
    dsp::PmDemod demod;
    demod.configure(dcfg, sample_rate);
    ggak::GgakFrameParser parser;

    constexpr std::size_t kBlock = 16384;
    std::vector<std::complex<float>> iq(kBlock);
    std::vector<int8_t> soft;
    std::vector<ggak::GgakFrame> frames;

    while (in) {
        if (is_cu8) {
            std::vector<uint8_t> raw(kBlock * 2);
            in.read(reinterpret_cast<char*>(raw.data()),
                    static_cast<std::streamsize>(raw.size()));
            const std::size_t got = static_cast<std::size_t>(in.gcount());
            const std::size_t n = got / 2;
            if (n == 0) {
                break;
            }
            for (std::size_t i = 0; i < n; ++i) {
                iq[i] = std::complex<float>(
                    (static_cast<float>(raw[2 * i]) - 127.4f) / 128.0f,
                    (static_cast<float>(raw[2 * i + 1]) - 127.4f) / 128.0f);
            }
            soft.clear();
            demod.process(iq.data(), n, soft);
        } else {
            in.read(reinterpret_cast<char*>(iq.data()),
                    static_cast<std::streamsize>(iq.size() *
                                                 sizeof(std::complex<float>)));
            const std::size_t got = static_cast<std::size_t>(in.gcount());
            const std::size_t n = got / sizeof(std::complex<float>);
            if (n == 0) {
                break;
            }
            soft.clear();
            demod.process(iq.data(), n, soft);
        }

        frames.clear();
        parser.push(soft.data(), soft.size(), frames);
        for (const ggak::GgakFrame& frame : frames) {
            ++summary.frames;
            if (frame.crc_ok) {
                ++summary.valid;
            } else {
                ++summary.invalid;
            }
            write_csv_row(csv, frame);
        }
    }

    summary.snr_db = demod.snr_db();
}

}  // namespace

int cmd_ggak(const Options& opts, const Args& args) {
    if (args.empty() || args[0] != "decode") {
        std::cerr << "error: ggak requires the 'decode' subcommand\n";
        return 2;
    }
    if (args.size() < 2) {
        std::cerr << "error: ggak decode requires a CADU/IQ directory\n";
        return 2;
    }

    const std::string dir = args[1];
    if (!fs::is_directory(dir)) {
        std::cerr << "error: not a directory: " << dir << "\n";
        return 2;
    }

    std::string config_error;
    Config cfg = Config::load(opts.config_path, &config_error);
    if (!config_error.empty()) {
        std::cerr << "warning: " << config_error << " (using defaults)\n";
    }

    const std::string csv_path = fs::join(dir, "ggak.csv");
    std::ofstream csv(csv_path, std::ios::trunc);
    if (!csv.is_open()) {
        std::cerr << "error: cannot write " << csv_path << "\n";
        return 1;
    }
    csv << "utc,id,crc_ok,frame_time\n";

    Summary summary;
    std::vector<std::string> files;
    collect_files(dir, files);

    for (const std::string& path : files) {
        const std::string name = fs::basename(path);
        if (has_suffix(name, ".cadu")) {
            decode_cadu_file(path, csv, summary);
        } else if (has_suffix(name, ".cf32")) {
            decode_iq_file(path, false, cfg, csv, summary);
        } else if (has_suffix(name, ".cu8")) {
            decode_iq_file(path, true, cfg, csv, summary);
        }
    }
    csv.close();

    std::cout << "ggak decode: " << summary.files << " file(s), "
              << summary.frames << " frame(s), " << summary.valid
              << " valid, " << summary.invalid << " invalid";
    if (summary.snr_db > 0.0) {
        std::cout << ", SNR " << summary.snr_db << " dB";
    }
    std::cout << "\nCSV: " << csv_path << "\n";
    return 0;
}

}  // namespace ed::cli
