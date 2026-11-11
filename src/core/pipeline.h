// ElektroDump — the tiny pipeline interface shared by GGAK and xRIT.
#pragma once

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

#include "core/ring_buffer.h"

namespace ed {

class Config;
class Metrics;

namespace image {
class ProductBuilder;
}

namespace ggak {
class GgakScienceStore;
}

enum class PipelineMode { GGAK, LRIT, HRIT };

const char* pipeline_mode_name(PipelineMode mode);

// Mode-specific constants handed to a pipeline at construction time.
struct PipelineParams {
    PipelineMode mode = PipelineMode::GGAK;
    uint64_t center_freq_hz = 0;
    uint32_t sample_rate_hz = 0;
    double symbol_rate = 0.0;
    std::string constellation;  // "bpsk" | "oqpsk" | "pm"
    double rrc_alpha = 0.5;
    double pll_bw = 0.0;
    int max_sps = 3;
    int cadu_bits = 8192;
    bool derandomize = true;
    int rs_interleave = 4;
    std::string rs_type = "rs223";
    bool rs_usecheck = true;
    bool nrzm = false;
};

struct PipelineStats {
    bool running = false;
    bool locked = false;
    double ber = 0.0;
    double snr_db = 0.0;
    uint64_t cadus_total = 0;
    uint64_t cadus_bad = 0;
    uint64_t files_total = 0;
    uint64_t files_recovered = 0;
};

// Buffers and counters handed to a pipeline at attach time. The pipeline is the
// primary consumer of `iq_ring` and fans IQ out to `waterfall_ring` so the FFT
// producer never competes for the primary ring.
struct PipelineIo {
    SpscRing<std::complex<float>>* iq_ring = nullptr;
    SpscRing<std::complex<float>>* waterfall_ring = nullptr;
    Metrics* metrics = nullptr;
    // Optional M6 image path; the xRIT pipeline submits finalized files here.
    image::ProductBuilder* products = nullptr;
    // Optional M9 GGAK science sink; the GGAK pipeline feeds SKL records here.
    ggak::GgakScienceStore* ggak_science = nullptr;
};

class IPipeline {
public:
    virtual ~IPipeline() = default;
    virtual PipelineMode mode() const = 0;
    virtual bool configure(const Config& cfg, const PipelineParams& params) = 0;
    virtual void attach_io(const PipelineIo& io) { (void)io; }
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual PipelineStats stats() const = 0;
    // Most recent demodulated symbols (empty when the mode has none yet).
    virtual void constellation(std::vector<std::complex<float>>& out) const {
        out.clear();
    }
};

}  // namespace ed
