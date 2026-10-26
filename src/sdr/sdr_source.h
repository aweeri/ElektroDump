// ElektroDump — SDR abstraction (PLAN.md D1).
#pragma once

#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ed::sdr {

struct SdrConfig {
    std::string driver = "airspy";  // rtlsdr | airspy | mock | file
    std::string serial;             // empty = first device
    uint64_t center_freq_hz = 1691000000;
    uint32_t sample_rate_hz = 1000000;
    int gain_tenth_db = 300;  // fixed gain in 0.1 dB; -1 = nearest available
    bool bias_tee = true;     // mandatory for the 1691/1693 MHz LNB
    int ppm = 0;

    // Airspy manual gain (used when driver == "airspy").
    int lna_gain = 10;    // 0..15
    int mixer_gain = 10;  // 0..15
    int vga_gain = 10;    // 0..15

    std::string file_path;
    std::string file_format = "int8";   // "int8" (rtl-sdr) | "float32"
    double mock_tone_offset_hz = 200000.0;
    double mock_noise_db = -30.0;

    // mock GGAK: PM/BPSK carrying valid 224-byte frames, exercises demod + CADU.
    bool mock_ggak = false;
    double mock_ggak_symbol_rate = 5000.0;
    double mock_ggak_mod_index = 0.5;
    double mock_ggak_snr_db = 20.0;
    double mock_ggak_carrier_offset_hz = 0.0;

    // mock LRIT: RRC-shaped BPSK carrying a synthetic Viterbi/RS-coded xRIT capture.
    bool mock_lrit = false;
    double mock_lrit_symbol_rate = 294000.0;
    double mock_lrit_snr_db = 20.0;
    double mock_lrit_carrier_phase = 0.4;

    // mock HRIT: RRC-shaped OQPSK with a carrier offset, exercises coarse acquisition.
    bool mock_hrit = false;
    double mock_hrit_symbol_rate = 1150000.0;
    double mock_hrit_snr_db = 20.0;
    double mock_hrit_carrier_phase = 0.4;
    double mock_hrit_carrier_offset_hz = 0.0;
};

struct SdrInfo {
    std::string driver;
    std::string name;
    std::string serial;
    uint32_t min_sample_rate = 0;
    uint32_t max_sample_rate = 0;
    std::vector<int> gains_tenth_db;
    bool has_bias_tee = false;
    bool available = false;  // backend compiled into this binary
};

struct SdrStats {
    uint64_t samples = 0;
    uint64_t overflows = 0;
    std::string last_error;
};

using IqSink = std::function<void(const std::complex<float>*, size_t)>;

class ISdrSource {
public:
    virtual ~ISdrSource() = default;

    virtual bool open(const SdrConfig& cfg) = 0;
    virtual bool start(IqSink sink) = 0;
    virtual void stop() = 0;
    virtual bool retune(uint64_t freq_hz) = 0;
    virtual bool set_gain(int tenth_db) = 0;
    virtual SdrInfo info() const = 0;
    virtual SdrStats stats() const = 0;

    // Live state for the status API.
    virtual uint64_t center_freq_hz() const = 0;
    virtual uint32_t sample_rate_hz() const = 0;
    virtual int gain_tenth_db() const = 0;
    virtual bool bias_tee() const = 0;
};

// Returns nullptr when the driver is unknown or its backend is not compiled in.
std::unique_ptr<ISdrSource> make_sdr(const std::string& driver);

bool backend_available(const std::string& driver);

// Enumerate devices across compiled-in backends; mock/file are always reported.
std::vector<SdrInfo> probe_devices();

std::vector<std::string> known_drivers();

}  // namespace ed::sdr
