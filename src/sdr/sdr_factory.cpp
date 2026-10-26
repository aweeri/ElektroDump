// ElektroDump — SDR factory and device enumeration (PLAN.md D1).
// Each backend lives in its own TU and exposes a constructor, an availability
// flag and a probe function; the factory is a handful of if branches.
#include "sdr/sdr_source.h"

namespace ed::sdr {

// Backend entry points (defined in the per-backend TUs).
std::unique_ptr<ISdrSource> make_rtlsdr();
bool rtlsdr_available();
std::vector<SdrInfo> probe_rtlsdr();

std::unique_ptr<ISdrSource> make_airspy();
bool airspy_available();
std::vector<SdrInfo> probe_airspy();

std::unique_ptr<ISdrSource> make_mock();
std::vector<SdrInfo> probe_mock();

std::unique_ptr<ISdrSource> make_file();
std::vector<SdrInfo> probe_file();

std::unique_ptr<ISdrSource> make_sdr(const std::string& driver) {
    if (driver == "rtlsdr") {
        return make_rtlsdr();
    }
    if (driver == "airspy") {
        return make_airspy();
    }
    if (driver == "mock") {
        return make_mock();
    }
    if (driver == "file") {
        return make_file();
    }
    return nullptr;
}

bool backend_available(const std::string& driver) {
    if (driver == "rtlsdr") {
        return rtlsdr_available();
    }
    if (driver == "airspy") {
        return airspy_available();
    }
    if (driver == "mock" || driver == "file") {
        return true;
    }
    return false;
}

std::vector<SdrInfo> probe_devices() {
    std::vector<SdrInfo> out;
    for (SdrInfo& info : probe_rtlsdr()) {
        out.push_back(std::move(info));
    }
    for (SdrInfo& info : probe_airspy()) {
        out.push_back(std::move(info));
    }
    // Hardware-free backends are always reported so `probe` works without a radio.
    for (SdrInfo& info : probe_mock()) {
        out.push_back(std::move(info));
    }
    for (SdrInfo& info : probe_file()) {
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<std::string> known_drivers() {
    return {"rtlsdr", "airspy", "mock", "file"};
}

}  // namespace ed::sdr
