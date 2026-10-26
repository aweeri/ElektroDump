#include "core/app.h"

#include <csignal>
#include <thread>
#include <vector>

#include "core/logging.h"
#include "core/pipeline_ggak.h"
#include "core/pipeline_xrit.h"
#include "core/time_util.h"
#include "image/product_builder.h"
#include "util/string_util.h"

namespace ed {

namespace {

std::atomic<App*> g_signal_app{nullptr};

void handle_signal(int /*signum*/) {
    App* app = g_signal_app.load(std::memory_order_relaxed);
    if (app != nullptr) {
        app->request_stop();
    }
}

}  // namespace

App::App() = default;

App::~App() {
    shutdown();
}

bool App::initialize(const std::string& config_path, std::string* error) {
    std::string load_error;
    config_ = Config::load(config_path, &load_error);
    if (!load_error.empty()) {
        if (error != nullptr) {
            *error = load_error;
        }
        return false;
    }

    // Apply logging settings before anything else logs.
    LogLevel level = LogLevel::Info;
    if (parse_log_level(config_.get_string("logging.level", "info"), level)) {
        Logger::instance().set_level(level);
    }
    const std::string log_file = config_.get_string("logging.file", "");
    if (!log_file.empty()) {
        Logger::instance().set_file(log_file);
    }

    if (!config_.loaded_from_file()) {
        ED_LOG(Warn, "config")
            << "config file '" << config_path
            << "' not found; using embedded defaults";
    }

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (!config_.validate(&errors, &warnings)) {
        for (const std::string& warning : warnings) {
            ED_LOG(Warn, "config") << warning;
        }
        for (const std::string& err : errors) {
            ED_LOG(Error, "config") << err;
        }
        if (error != nullptr) {
            *error = "configuration validation failed (" +
                     std::to_string(errors.size()) + " error(s))";
        }
        return false;
    }
    for (const std::string& warning : warnings) {
        ED_LOG(Warn, "config") << warning;
    }

    const std::string mode =
        str::to_lower(config_.get_string("run.mode", "ggak"));
    if (mode == "lrit") {
        mode_ = PipelineMode::LRIT;
    } else if (mode == "hrit") {
        mode_ = PipelineMode::HRIT;
    } else {
        mode_ = PipelineMode::GGAK;
    }
    mode_code_.store(static_cast<int>(mode_), std::memory_order_relaxed);

    initialized_ = true;
    ED_LOG(Info, "app") << kAppName << " " << kVersion << " initialized ("
                        << time::iso8601_now(false) << "), mode "
                        << pipeline_mode_name(mode_);
    return true;
}

sdr::SdrConfig App::sdr_config() const {
    sdr::SdrConfig cfg;
    cfg.driver = config_.get_string("sdr.driver", "airspy");
    cfg.serial = config_.get_string("sdr.serial", "");
    cfg.gain_tenth_db = static_cast<int>(config_.get_int("sdr.gain_tenth_db", 300));
    cfg.bias_tee = config_.get_bool("sdr.bias_tee", true);
    cfg.ppm = static_cast<int>(config_.get_int("sdr.ppm", 0));
    cfg.lna_gain = static_cast<int>(config_.get_int("sdr.airspy.lna_gain", 10));
    cfg.mixer_gain = static_cast<int>(config_.get_int("sdr.airspy.mixer_gain", 10));
    cfg.vga_gain = static_cast<int>(config_.get_int("sdr.airspy.vga_gain", 10));
    cfg.file_path = config_.get_string("sdr.file_path", "");
    cfg.file_format = config_.get_string("sdr.file_format", "int8");
    cfg.mock_tone_offset_hz = config_.get_double("sdr.mock.tone_offset_hz", 200000.0);
    cfg.mock_noise_db = config_.get_double("sdr.mock.noise_db", -30.0);
    cfg.mock_ggak = config_.get_bool("sdr.mock.ggak", false);
    cfg.mock_ggak_symbol_rate =
        config_.get_double("ggak.symbol_rate", 5000.0);
    cfg.mock_ggak_mod_index =
        config_.get_double("ggak.mock.mod_index", 0.5);
    cfg.mock_ggak_snr_db = config_.get_double("ggak.mock.snr_db", 20.0);
    cfg.mock_ggak_carrier_offset_hz =
        config_.get_double("ggak.mock.carrier_offset_hz", 0.0);

    if (mode_ == PipelineMode::GGAK) {
        cfg.center_freq_hz = static_cast<uint64_t>(
            config_.get_int("frequencies.ggak_hz", 1693000000));
        cfg.sample_rate_hz = static_cast<uint32_t>(
            config_.get_int("ggak.sample_rate_hz", 3000000));
        cfg.mock_ggak = config_.get_bool("sdr.mock.ggak", false);
        cfg.mock_ggak_symbol_rate = config_.get_double("ggak.symbol_rate", 5000.0);
        cfg.mock_ggak_mod_index = config_.get_double("ggak.mock.mod_index", 0.5);
        cfg.mock_ggak_snr_db = config_.get_double("ggak.mock.snr_db", 20.0);
        cfg.mock_ggak_carrier_offset_hz =
            config_.get_double("ggak.mock.carrier_offset_hz", 0.0);
    } else {
        const bool hrit = mode_ == PipelineMode::HRIT;
        const char* sec = hrit ? "hrit" : "lrit";
        cfg.center_freq_hz = static_cast<uint64_t>(config_.get_int(
            hrit ? "frequencies.hrit_hz" : "frequencies.lrit_hz", 1691000000));
        cfg.sample_rate_hz = static_cast<uint32_t>(config_.get_int(
            std::string(sec) + ".sample_rate_hz", hrit ? 6000000 : 1000000));
        cfg.mock_lrit = config_.get_bool("sdr.mock.lrit", false);
        cfg.mock_lrit_symbol_rate =
            config_.get_double(std::string(sec) + ".symbol_rate",
                               hrit ? 1150000.0 : 294000.0);
        cfg.mock_lrit_snr_db =
            config_.get_double(std::string(sec) + ".mock.snr_db", 20.0);
        cfg.mock_lrit_carrier_phase =
            config_.get_double(std::string(sec) + ".mock.carrier_phase", 0.4);

        // HRIT mock: OQPSK with an optional carrier offset (exercises coarse acq).
        cfg.mock_hrit = config_.get_bool("sdr.mock.hrit", false);
        cfg.mock_hrit_symbol_rate =
            config_.get_double("hrit.symbol_rate", 1150000.0);
        cfg.mock_hrit_snr_db =
            config_.get_double("hrit.mock.snr_db", 20.0);
        cfg.mock_hrit_carrier_phase =
            config_.get_double("hrit.mock.carrier_phase", 0.4);
        cfg.mock_hrit_carrier_offset_hz =
            config_.get_double("hrit.mock.carrier_offset_hz", 0.0);
    }
    return cfg;
}

PipelineParams App::ggak_params() const {
    PipelineParams params;
    params.mode = PipelineMode::GGAK;
    params.center_freq_hz = static_cast<uint64_t>(
        config_.get_int("frequencies.ggak_hz", 1693000000));
    params.sample_rate_hz = static_cast<uint32_t>(
        config_.get_int("ggak.sample_rate_hz", 3000000));
    params.symbol_rate = config_.get_double("ggak.symbol_rate", 5000.0);
    params.constellation = "pm";
    params.rrc_alpha = config_.get_double("ggak.rrc_alpha", 0.5);
    params.pll_bw = config_.get_double("ggak.pll_bw", 0.02);
    params.max_sps = 8;
    params.cadu_bits = 1792;
    params.derandomize = false;
    params.rs_interleave = 0;
    params.rs_type = "none";
    params.rs_usecheck = false;
    params.nrzm = false;
    return params;
}

PipelineParams App::xrit_params() const {
    const bool hrit = mode_ == PipelineMode::HRIT;
    const char* sec = hrit ? "hrit" : "lrit";
    PipelineParams params;
    params.mode = hrit ? PipelineMode::HRIT : PipelineMode::LRIT;
    params.center_freq_hz = static_cast<uint64_t>(config_.get_int(
        hrit ? "frequencies.hrit_hz" : "frequencies.lrit_hz", 1691000000));
    params.sample_rate_hz = static_cast<uint32_t>(config_.get_int(
        std::string(sec) + ".sample_rate_hz", hrit ? 6000000 : 1000000));
    params.symbol_rate = config_.get_double(std::string(sec) + ".symbol_rate",
                                            hrit ? 1150000.0 : 294000.0);
    params.constellation =
        config_.get_string(std::string(sec) + ".constellation", hrit ? "oqpsk" : "bpsk");
    params.rrc_alpha = config_.get_double(std::string(sec) + ".rrc_alpha", 0.5);
    params.pll_bw = config_.get_double(std::string(sec) + ".pll_bw",
                                       hrit ? 0.001 : 0.0145);
    params.max_sps = static_cast<int>(config_.get_int(std::string(sec) + ".max_sps", 3));
    params.cadu_bits = static_cast<int>(config_.get_int("decode.cadu_bits", 8192));
    params.derandomize = config_.get_bool("decode.derandomize", true);
    params.rs_interleave = static_cast<int>(config_.get_int("decode.rs_interleave", 4));
    params.rs_type = config_.get_string("decode.rs_type", "rs223");
    params.rs_usecheck = config_.get_bool("decode.rs_usecheck", true);
    params.nrzm = false;
    return params;
}

void App::start_pipeline(PipelineMode mode,
                         const scheduler::ScheduleEntry* entry) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (pipeline_ != nullptr) {
        pipeline_->stop();
        pipeline_.reset();
    }
    if (products_ != nullptr) {
        products_->stop();
        products_.reset();
    }
    mode_ = mode;
    mode_code_.store(static_cast<int>(mode), std::memory_order_relaxed);

    PipelineParams params =
        (mode == PipelineMode::GGAK) ? ggak_params() : xrit_params();
    if (entry != nullptr) {
        if (entry->frequency_hz != 0) {
            params.center_freq_hz = entry->frequency_hz;
        }
        if (entry->symbol_rate > 0.0) {
            params.symbol_rate = entry->symbol_rate;
        }
        if (entry->sample_rate_hz != 0) {
            params.sample_rate_hz = entry->sample_rate_hz;
        }
        if (!entry->constellation.empty()) {
            params.constellation = entry->constellation;
        }
    }

    if (mode == PipelineMode::GGAK) {
        pipeline_ = std::make_unique<GgakPipeline>();
    } else {
        pipeline_ = std::make_unique<XritPipeline>();
    }
    pipeline_->configure(config_, params);

    if (mode != PipelineMode::GGAK) {
        // M6: the image worker consumes finalized xRIT files.
        products_ = std::make_unique<image::ProductBuilder>();
        std::string product_error;
        if (products_->configure(config_, &product_error)) {
            products_->set_product_callback(
                [this](const output::ProductMeta& meta) {
                    ED_LOG(Info, "image")
                        << "product " << meta.product << " -> " << meta.path;
                    metrics_.add_product();
                    // M9: fire the product.created webhook (non-blocking).
                    if (webhook_ != nullptr) {
                        webhook_->notify_product(
                            meta, config_.get_string("api.base_url", ""));
                    }
                });
            products_->start();
        } else {
            ED_LOG(Warn, "image")
                << "product builder disabled: " << product_error;
            products_.reset();
        }
    }

    PipelineIo io;
    io.iq_ring = &iq_ring_;
    io.waterfall_ring = &waterfall_ring_;
    io.metrics = &metrics_;
    io.products = products_.get();
    io.ggak_science = ggak_science_.get();
    pipeline_->attach_io(io);
    pipeline_->start();
}

void App::switch_mode(PipelineMode mode,
                      const scheduler::ScheduleEntry* entry) {
    if (sdr_ != nullptr) {
        uint64_t freq = 0;
        if (entry != nullptr && entry->frequency_hz != 0) {
            freq = entry->frequency_hz;
        } else {
            const char* key = mode == PipelineMode::GGAK
                                  ? "frequencies.ggak_hz"
                                  : (mode == PipelineMode::HRIT
                                         ? "frequencies.hrit_hz"
                                         : "frequencies.lrit_hz");
            freq = static_cast<uint64_t>(config_.get_int(key, 0));
        }
        if (freq != 0) {
            sdr_->retune(freq);
        }
    }
    start_pipeline(mode, entry);
    std::string label = pipeline_mode_name(mode);
    if (entry != nullptr) {
        label += " (" + entry->name + ")";
    }
    ED_LOG(Info, "scheduler") << "mode -> " << label;
}

int App::run() { return run_loop(true); }

int App::serve() { return run_loop(false); }

int App::run_loop(bool with_sdr) {
    if (!initialized_) {
        ED_LOG(Error, "app") << "run() called before initialize()";
        return 1;
    }
    running_.store(true, std::memory_order_relaxed);
    stop_requested_.store(false, std::memory_order_relaxed);
    start_time_ = std::chrono::steady_clock::now();

    // GGAK science series (fed by the GGAK pipeline) and the webhook dispatcher
    // (fires on product creation); both exist in `serve` so the API can report them.
    ggak_science_ = std::make_unique<ggak::GgakScienceStore>();
    ggak_science_->configure();
    webhook_ = std::make_unique<web::WebhookDispatcher>();
    std::string webhook_error;
    if (!webhook_->configure(config_, &webhook_error)) {
        ED_LOG(Warn, "webhook") << "webhook disabled: " << webhook_error;
        webhook_.reset();
    } else {
        webhook_->start();
    }

    // Standalone store view over storage.root; created before the pipeline so
    // its stale-tmp sweep cannot race the builder.
    store_ = std::make_unique<output::ProductStore>();
    std::string store_error;
    if (!store_->configure(config_, &store_error)) {
        ED_LOG(Debug, "image") << "product store disabled: " << store_error;
        store_.reset();
    }

    if (with_sdr) {
        const sdr::SdrConfig scfg = sdr_config();
        sdr_ = sdr::make_sdr(scfg.driver);
        if (sdr_ == nullptr) {
            sdr_error_ = "SDR driver '" + scfg.driver +
                         "' is not available in this build";
            ED_LOG(Error, "sdr") << sdr_error_;
        } else if (!sdr_->open(scfg)) {
            sdr_error_ = "failed to open SDR: " + sdr_->stats().last_error;
            ED_LOG(Error, "sdr") << sdr_error_;
            sdr_.reset();
        } else {
            const std::size_t fft_size = static_cast<std::size_t>(
                config_.get_int("performance.fft_size", 1024));
            const int fft_rate =
                static_cast<int>(config_.get_int("performance.fft_rate_hz", 10));
            waterfall_.configure(fft_size, fft_rate, scfg.sample_rate_hz,
                                 scfg.center_freq_hz);
            // The waterfall consumes the fan-out ring, not the primary IQ ring.
            waterfall_.start(&waterfall_ring_);

            // The pipeline is the primary IQ consumer and fans blocks out to the
            // waterfall ring; the scheduler swaps the mode later.
            start_pipeline(mode_, nullptr);

            const bool started = sdr_->start(
                [this](const std::complex<float>* iq, std::size_t n) {
                    metrics_.add_iq(n);
                    const std::size_t cap = iq_ring_.capacity();
                    const std::size_t before = iq_ring_.size();
                    iq_ring_.write(iq, n);
                    if (before + n > cap) {
                        metrics_.add_overflow();
                    }
                });
            if (!started) {
                sdr_error_ = "failed to start SDR: " + sdr_->stats().last_error;
                ED_LOG(Error, "sdr") << sdr_error_;
                pipeline_->stop();
                pipeline_.reset();
                waterfall_.stop();
                sdr_.reset();
            } else {
                const sdr::SdrInfo info = sdr_->info();
                ED_LOG(Info, "sdr")
                    << "opened " << info.driver << " (" << info.name << ") at "
                    << scfg.center_freq_hz << " Hz, " << scfg.sample_rate_hz
                    << " sps, gain " << scfg.gain_tenth_db << " (0.1 dB)";
            }
        }
    }

    // The scheduler drives mode switches only when the SDR is running; in
    // `serve` it is exposed read-only via the API.
    cadu_ = std::make_unique<scheduler::CaduLifecycle>();
    std::string cadu_error;
    if (!cadu_->configure(config_, &cadu_error)) {
        ED_LOG(Warn, "scheduler") << "CADU lifecycle disabled: " << cadu_error;
        cadu_.reset();
    }

    scheduler_ = std::make_unique<scheduler::Scheduler>();
    std::string scheduler_error;
    if (!scheduler_->configure(config_, &scheduler_error)) {
        ED_LOG(Warn, "scheduler") << "scheduler disabled: " << scheduler_error;
        scheduler_.reset();
    }

    // The web server starts even when the SDR failed, so the error is visible.
    web::WebContext ctx;
    ctx.config = &config_;
    ctx.metrics = &metrics_;
    ctx.sdr = sdr_.get();
    ctx.waterfall = with_sdr ? &waterfall_ : nullptr;
    ctx.pipeline = pipeline_.get();
    ctx.products = store_.get();
    ctx.satellite = config_.get_string("satellite.id", "elektro-l3");
    ctx.mode = pipeline_mode_name(mode_);
    ctx.mode_code = &mode_code_;
    ctx.scheduler = scheduler_.get();
    ctx.cadu = cadu_.get();
    ctx.ggak = ggak_science_.get();
    ctx.webhook = webhook_.get();
    ctx.pipeline_snapshot = [this]() -> web::PipelineSnapshot {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        web::PipelineSnapshot snap;
        if (pipeline_ != nullptr) {
            snap.present = true;
            snap.stats = pipeline_->stats();
        }
        return snap;
    };
    ctx.sdr_error = sdr_error_;
    ctx.start_time = start_time_;
    if (!web_.start(config_, ctx)) {
        ED_LOG(Warn, "web") << "web server not started";
    }

    // Start the scheduler only after the web context is built, so the first
    // transition cannot race the run_loop startup.
    if (scheduler_ != nullptr && with_sdr) {
        scheduler_->set_current_mode(mode_);
        scheduler_->set_transition(
            [this](PipelineMode m, const scheduler::ScheduleEntry* e) {
                switch_mode(m, e);
            });
        scheduler_->set_janitor([this] {
            if (cadu_ != nullptr) {
                cadu_->janitor();
            }
            if (webhook_ != nullptr) {
                webhook_->retry_pending();
            }
        });
        scheduler_->start();
    }

    g_signal_app.store(this, std::memory_order_relaxed);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    ED_LOG(Info, "app") << "running; press Ctrl-C to stop";
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ED_LOG(Info, "app") << "stop requested; shutting down";
    web_.stop();
    if (scheduler_ != nullptr) {
        scheduler_->stop();
        scheduler_.reset();
    }
    if (sdr_ != nullptr) {
        sdr_->stop();
        sdr_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        if (pipeline_ != nullptr) {
            pipeline_->stop();
            pipeline_.reset();
        }
        if (products_ != nullptr) {
            products_->stop();
            products_.reset();
        }
    }
    // Stop the dispatcher only after the product worker (which may still invoke
    // the product-created callback) has joined.
    if (webhook_ != nullptr) {
        webhook_->stop();
        webhook_.reset();
    }
    store_.reset();
    ggak_science_.reset();
    waterfall_.stop();
    g_signal_app.store(nullptr, std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);
    return 0;
}

void App::request_stop() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

void App::shutdown() {
    if (!initialized_) {
        return;
    }
    request_stop();
    web_.stop();
    if (scheduler_ != nullptr) {
        scheduler_->stop();
        scheduler_.reset();
    }
    if (sdr_ != nullptr) {
        sdr_->stop();
        sdr_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        if (pipeline_ != nullptr) {
            pipeline_->stop();
            pipeline_.reset();
        }
        if (products_ != nullptr) {
            products_->stop();
            products_.reset();
        }
    }
    if (webhook_ != nullptr) {
        webhook_->stop();
        webhook_.reset();
    }
    store_.reset();
    ggak_science_.reset();
    waterfall_.stop();
    running_.store(false, std::memory_order_relaxed);
    ED_LOG(Info, "app") << "shutdown complete";
    Logger::instance().flush();
    initialized_ = false;
}

}  // namespace ed
