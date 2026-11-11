#include "core/pipeline_ggak.h"

#include <algorithm>
#include <chrono>
#include <complex>
#include <thread>

#include "core/config.h"
#include "core/logging.h"
#include "core/metrics.h"
#include "core/thread_util.h"
#include "dsp/snr_estimator.h"
#include "ggak/ggak_science.h"

namespace ed {

namespace {

constexpr std::size_t kIqBlock = 16384;

}  // namespace

GgakPipeline::GgakPipeline() = default;

GgakPipeline::~GgakPipeline() { stop(); }

bool GgakPipeline::configure(const Config& cfg, const PipelineParams& params) {
    config_ = &cfg;
    params_ = params;

    spool_dir_ = cfg.get_string("ggak.spool_dir", "data/spool/ggak");
    rotate_seconds_ =
        static_cast<int>(cfg.get_int("ggak.rotate_seconds", 3600));
    time_offset_seconds_ = cfg.get_double("ggak.time_offset_seconds", -10800.0);

    dsp::PmDemodConfig demod_cfg;
    demod_cfg.symbol_rate = params.symbol_rate > 0.0
                                ? params.symbol_rate
                                : cfg.get_double("ggak.symbol_rate", 5000.0);
    demod_cfg.rrc_alpha = params.rrc_alpha > 0.0
                              ? params.rrc_alpha
                              : cfg.get_double("ggak.rrc_alpha", 0.5);
    demod_cfg.pll_bw = params.pll_bw > 0.0
                           ? params.pll_bw
                           : cfg.get_double("ggak.pll_bw", 0.02);
    demod_cfg.costas_bw = cfg.get_double("ggak.costas_bw", 0.02);
    demod_cfg.max_carrier_offset_hz =
        cfg.get_double("ggak.max_carrier_offset_hz", 50000.0);
    demod_cfg.subcarrier_hz = cfg.get_double("ggak.subcarrier_hz", 0.0);
    demod_cfg.freq_offset_hz = params.freq_offset_hz;
    demod_cfg.subcarrier_enabled =
        cfg.get_bool("ggak.subcarrier_enabled", true);
    demod_cfg.target_sps =
        static_cast<int>(cfg.get_int("ggak.target_sps", 8));
    demod_cfg.rrc_taps = static_cast<int>(cfg.get_int("ggak.rrc_taps", 33));

    const uint32_t sample_rate =
        params.sample_rate_hz > 0
            ? params.sample_rate_hz
            : static_cast<uint32_t>(cfg.get_int("ggak.sample_rate_hz", 3000000));
    demod_.configure(demod_cfg, sample_rate);
    parser_.reset();
    store_.configure(spool_dir_, rotate_seconds_);

    ED_LOG(Info, "ggak") << "configured: " << demod_cfg.symbol_rate
                         << " baud, " << sample_rate << " sps, spool "
                         << spool_dir_ << ", time offset "
                         << time_offset_seconds_ << " s";
    return true;
}

void GgakPipeline::attach_io(const PipelineIo& io) { io_ = io; }

void GgakPipeline::start() {
    if (running_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    last_stats_ = std::chrono::steady_clock::now();
    last_valid_frame_ = std::chrono::steady_clock::time_point{};
    thread_ = std::thread([this] { dsp_loop(); });
}

void GgakPipeline::stop() {
    if (!running_.exchange(false, std::memory_order_relaxed)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    store_.flush();
    store_.close();
}

void GgakPipeline::dsp_loop() {
    thread::set_name("ggak-dsp");
    thread::set_nice(-10);

    std::vector<std::complex<float>> iq(kIqBlock);
    std::vector<int8_t> soft;
    std::vector<ggak::GgakFrame> frames;
    soft.reserve(kIqBlock / 4);
    frames.reserve(8);

    while (running_.load(std::memory_order_relaxed)) {
        if (io_.iq_ring == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const std::size_t n = io_.iq_ring->read(iq.data(), iq.size());
        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Fan the same IQ block out to the waterfall producer.
        if (io_.waterfall_ring != nullptr) {
            io_.waterfall_ring->write(iq.data(), n);
        }

        soft.clear();
        demod_.process(iq.data(), n, soft);

        frames.clear();
        parser_.push(soft.data(), soft.size(), frames);

        for (const ggak::GgakFrame& frame : frames) {
            store_.write(frame);
            if (io_.ggak_science != nullptr) {
                io_.ggak_science->add_frame(frame);
            }
            last_valid_frame_ = std::chrono::steady_clock::now();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_stats_ >= std::chrono::seconds(1)) {
            last_stats_ = now;
            publish_stats();
        }
    }

    publish_stats();
}

void GgakPipeline::publish_stats() {
    // Flush the CADU spool on the 1 Hz tick so the file is visible to the offline
    // decoder and the web UI without waiting for a 64 KB buffer.
    store_.flush();

    const double snr = demod_.snr_db();
    const bool recent_frame =
        last_valid_frame_ != std::chrono::steady_clock::time_point{} &&
        (std::chrono::steady_clock::now() - last_valid_frame_) <
            std::chrono::seconds(5);
    const bool locked = demod_.locked() && recent_frame;
    const double ber = dsp::bpsk_ber_from_snr_db(snr);

    snr_db_.store(snr, std::memory_order_relaxed);
    locked_.store(locked, std::memory_order_relaxed);
    ber_.store(ber, std::memory_order_relaxed);
    cadus_total_.store(parser_.frames_total(), std::memory_order_relaxed);
    cadus_bad_.store(parser_.frames_bad(), std::memory_order_relaxed);

    if (io_.metrics != nullptr) {
        io_.metrics->set_snr(snr);
        io_.metrics->set_locked(locked);
        io_.metrics->set_ber(ber);
        io_.metrics->set_cadus(parser_.frames_total(), parser_.frames_bad());
    }
}

void GgakPipeline::constellation(std::vector<std::complex<float>>& out) const {
    demod_.constellation(out);
}

PipelineStats GgakPipeline::stats() const {
    PipelineStats s;
    s.running = running_.load(std::memory_order_relaxed);
    s.locked = locked_.load(std::memory_order_relaxed);
    s.demod_locked = demod_.locked();
    s.ber = ber_.load(std::memory_order_relaxed);
    s.snr_db = snr_db_.load(std::memory_order_relaxed);
    s.cadus_total = cadus_total_.load(std::memory_order_relaxed);
    s.cadus_bad = cadus_bad_.load(std::memory_order_relaxed);
    s.files_total = 0;
    s.files_recovered = 0;
    return s;
}

}  // namespace ed
