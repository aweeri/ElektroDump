// ElektroDump — synthetic SDR backend (hardware-free testing).
// Generates a tone plus Gaussian noise, throttled to real time so the waterfall
// and status API behave as with a radio attached. Always compiled in.
#include "sdr/sdr_source.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

#include "core/thread_util.h"
#include "dsp/modulate_psk.h"
#include "xrit/synth.h"

namespace ed::sdr {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kBlock = 16384;

}  // namespace

class MockSource : public ISdrSource {
public:
    ~MockSource() override { stop(); }

    bool open(const SdrConfig& cfg) override {
        cfg_ = cfg;
        center_.store(cfg.center_freq_hz, std::memory_order_relaxed);
        rate_.store(cfg.sample_rate_hz, std::memory_order_relaxed);
        gain_.store(cfg.gain_tenth_db, std::memory_order_relaxed);
        bias_.store(cfg.bias_tee, std::memory_order_relaxed);
        return true;
    }

    bool start(IqSink sink) override {
        sink_ = std::move(sink);
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] { loop(); });
        return true;
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_relaxed)) {
            if (thread_.joinable()) {
                thread_.join();
            }
            return;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool retune(uint64_t freq_hz) override {
        center_.store(freq_hz, std::memory_order_relaxed);
        return true;
    }

    // The synthetic generator captures the sample rate when its loop starts, so
    // a live change would desynchronise the stream from the reported rate.
    // Refuse it rather than lie; a restart applies the new rate.
    bool set_sample_rate(uint32_t rate_hz) override {
        if (running_.load(std::memory_order_relaxed)) {
            last_error_ = "mock backend cannot change sample rate while running";
            return false;
        }
        rate_.store(rate_hz, std::memory_order_relaxed);
        cfg_.sample_rate_hz = rate_hz;
        return true;
    }

    bool set_gain(int tenth_db) override {
        gain_.store(tenth_db, std::memory_order_relaxed);
        return true;
    }

    SdrInfo info() const override {
        SdrInfo info;
        info.driver = "mock";
        info.name = "Synthetic IQ (mock)";
        info.serial = "mock-0";
        info.min_sample_rate = 250000;
        info.max_sample_rate = 10000000;
        info.has_bias_tee = false;
        info.available = true;
        for (int g = 0; g <= 400; g += 10) {
            info.gains_tenth_db.push_back(g);
        }
        return info;
    }

    SdrStats stats() const override {
        SdrStats s;
        s.samples = samples_.load(std::memory_order_relaxed);
        s.overflows = overflows_.load(std::memory_order_relaxed);
        s.last_error = last_error_;
        return s;
    }

    uint64_t center_freq_hz() const override {
        return center_.load(std::memory_order_relaxed);
    }
    uint32_t sample_rate_hz() const override {
        return rate_.load(std::memory_order_relaxed);
    }
    int gain_tenth_db() const override {
        return gain_.load(std::memory_order_relaxed);
    }
    bool bias_tee() const override {
        return bias_.load(std::memory_order_relaxed);
    }

private:
    void loop() {
        thread::set_name("sdr-mock");
        if (cfg_.mock_hrit) {
            loop_hrit();
        } else if (cfg_.mock_lrit) {
            loop_lrit();
        } else if (cfg_.mock_ggak) {
            loop_ggak();
        } else {
            loop_tone();
        }
    }

    void loop_tone() {
        const double fs = static_cast<double>(cfg_.sample_rate_hz);
        const double dphi = 2.0 * kPi * cfg_.mock_tone_offset_hz / fs;
        const double noise = std::pow(10.0, cfg_.mock_noise_db / 20.0);
        std::mt19937 rng(0xE1EC7);
        std::normal_distribution<float> nd(0.0f, static_cast<float>(noise));

        std::vector<std::complex<float>> buf(kBlock);
        double phase = 0.0;
        const auto period = std::chrono::nanoseconds(
            static_cast<long long>(1e9 * static_cast<double>(kBlock) / fs));

        while (running_.load(std::memory_order_relaxed)) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kBlock; ++i) {
                buf[i] = std::complex<float>(
                    static_cast<float>(std::cos(phase)) + nd(rng),
                    static_cast<float>(std::sin(phase)) + nd(rng));
                phase += dphi;
                if (phase > 2.0 * kPi) {
                    phase -= 2.0 * kPi;
                }
            }
            samples_.fetch_add(kBlock, std::memory_order_relaxed);
            if (sink_) {
                sink_(buf.data(), kBlock);
            }
            const auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
    }

    // Synthetic GGAK-M downlink: PM subcarrier carrying BPSK from valid 224-byte
    // frames (sum-CRC included), so the demod -> frame-sync -> CADU path runs.
    void loop_ggak() {
        const double fs = static_cast<double>(cfg_.sample_rate_hz);
        const double symbol_rate = cfg_.mock_ggak_symbol_rate > 0.0
                                       ? cfg_.mock_ggak_symbol_rate
                                       : 5000.0;
        const double sps = fs / symbol_rate;
        const double beta = cfg_.mock_ggak_mod_index;
        const double carrier_offset = cfg_.mock_ggak_carrier_offset_hz;
        const double noise_power =
            std::pow(10.0, -cfg_.mock_ggak_snr_db / 10.0);
        const double noise_std = std::sqrt(noise_power / 2.0);

        std::mt19937 rng(0xE1EC7);
        std::normal_distribution<float> nd(0.0f,
                                           static_cast<float>(noise_std));

        // Build a small loop of valid frames.
        constexpr int kFrameCount = 16;
        constexpr std::size_t kFrameBytes = 224;
        std::vector<uint8_t> frames(kFrameCount * kFrameBytes);
        for (int f = 0; f < kFrameCount; ++f) {
            uint8_t* raw = frames.data() + f * kFrameBytes;
            raw[0] = 0xAA;
            raw[1] = 0xAA;
            raw[2] = 0xAA;
            raw[3] = 0xAA;
            raw[4] = 48;  // SKL
            raw[5] = static_cast<uint8_t>((f >> 8) & 0xFF);
            raw[6] = static_cast<uint8_t>(f & 0xFF);
            raw[7] = 0;
            raw[8] = 0;
            const uint32_t ts = 0x10000000u + static_cast<uint32_t>(f);
            raw[9] = static_cast<uint8_t>((ts >> 24) & 0xFF);
            raw[10] = static_cast<uint8_t>((ts >> 16) & 0xFF);
            raw[11] = static_cast<uint8_t>((ts >> 8) & 0xFF);
            raw[12] = static_cast<uint8_t>(ts & 0xFF);
            for (std::size_t i = 13; i < 222; ++i) {
                raw[i] = static_cast<uint8_t>((i + static_cast<std::size_t>(f)) &
                                              0xFF);
            }
            uint16_t sum = 0;
            for (std::size_t i = 0; i < 222; ++i) {
                sum = static_cast<uint16_t>(sum + raw[i]);
            }
            raw[222] = static_cast<uint8_t>((sum >> 8) & 0xFF);
            raw[223] = static_cast<uint8_t>(sum & 0xFF);
        }

        const std::size_t total_bits = frames.size() * 8;
        std::size_t bit_index = 0;
        double sample_index = 0.0;
        double sub_phase = 0.0;
        double car_phase = 0.0;
        const double sub_delta = 2.0 * kPi * symbol_rate / fs;
        const double car_delta = 2.0 * kPi * carrier_offset / fs;

        std::vector<std::complex<float>> buf(kBlock);
        const auto period = std::chrono::nanoseconds(
            static_cast<long long>(1e9 * static_cast<double>(kBlock) / fs));

        while (running_.load(std::memory_order_relaxed)) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kBlock; ++i) {
                const std::size_t byte_index =
                    (bit_index / 8) % frames.size();
                const int bit_in_byte = 7 - static_cast<int>(bit_index % 8);
                const int bit = (frames[byte_index] >> bit_in_byte) & 1;
                const double d = bit ? 1.0 : -1.0;

                const double phase =
                    car_phase + beta * d * std::cos(sub_phase);
                buf[i] = std::complex<float>(
                    static_cast<float>(std::cos(phase)) + nd(rng),
                    static_cast<float>(std::sin(phase)) + nd(rng));

                sub_phase += sub_delta;
                if (sub_phase > 2.0 * kPi) {
                    sub_phase -= 2.0 * kPi;
                }
                car_phase += car_delta;
                if (car_phase > 2.0 * kPi) {
                    car_phase -= 2.0 * kPi;
                }

                sample_index += 1.0;
                if (sample_index >= sps) {
                    sample_index -= sps;
                    bit_index = (bit_index + 1) % total_bits;
                }
            }
            samples_.fetch_add(kBlock, std::memory_order_relaxed);
            if (sink_) {
                sink_(buf.data(), kBlock);
            }
            const auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
    }

    // Synthetic LRIT downlink: RRC-shaped BPSK carrying a valid xRIT capture,
    // looped so the demod/FEC/xRIT chain can lock.
    void loop_lrit() {
        const double fs = static_cast<double>(cfg_.sample_rate_hz);
        const double baud = cfg_.mock_lrit_symbol_rate > 0.0
                                ? cfg_.mock_lrit_symbol_rate
                                : 294000.0;
        const double sps = fs / baud;
        const ed::xrit::SynthLrit synth =
            ed::xrit::make_synthetic_lrit(200, 1, 20000);
        std::vector<std::complex<float>> full(
            ed::dsp::modulated_bpsk_samples(synth.tx_bits.size(), sps));
        if (full.empty()) {
            return;
        }
        ed::dsp::modulate_bpsk(synth.tx_bits.data(), synth.tx_bits.size(), sps,
                               0.5, 33, cfg_.mock_lrit_carrier_phase,
                               full.data(), full.size());

        const double noise_power = std::pow(10.0, -cfg_.mock_lrit_snr_db / 10.0);
        const double noise_std = std::sqrt(noise_power / 2.0);
        std::mt19937 rng(0x1EC7);
        std::normal_distribution<float> nd(0.0f,
                                           static_cast<float>(noise_std));

        std::vector<std::complex<float>> buf(kBlock);
        const auto period = std::chrono::nanoseconds(
            static_cast<long long>(1e9 * static_cast<double>(kBlock) / fs));
        std::size_t pos = 0;
        while (running_.load(std::memory_order_relaxed)) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kBlock; ++i) {
                buf[i] = full[pos] +
                         std::complex<float>(nd(rng), nd(rng));
                pos = (pos + 1) % full.size();
            }
            samples_.fetch_add(kBlock, std::memory_order_relaxed);
            if (sink_) {
                sink_(buf.data(), kBlock);
            }
            const auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
    }

    // Synthetic HRIT downlink: RRC-shaped OQPSK carrying a valid xRIT capture,
    // with an optional carrier offset to exercise coarse acquisition; looped so
    // the demod/FEC/xRIT chain can lock.
    void loop_hrit() {
        const double fs = static_cast<double>(cfg_.sample_rate_hz);
        const double baud = cfg_.mock_hrit_symbol_rate > 0.0
                                ? cfg_.mock_hrit_symbol_rate
                                : 1150000.0;
        const double sps = fs / baud;
        const ed::xrit::SynthLrit synth =
            ed::xrit::make_synthetic_lrit(200, 1, 20000);
        std::vector<std::complex<float>> full(
            ed::dsp::modulated_oqpsk_samples(synth.tx_bits.size(), sps));
        if (full.empty()) {
            return;
        }
        ed::dsp::modulate_oqpsk(synth.tx_bits.data(), synth.tx_bits.size(), sps,
                                0.5, 33, cfg_.mock_hrit_carrier_phase,
                                full.data(), full.size());

        const double noise_power = std::pow(10.0, -cfg_.mock_hrit_snr_db / 10.0);
        const double noise_std = std::sqrt(noise_power / 2.0);
        std::mt19937 rng(0x1EC7);
        std::normal_distribution<float> nd(0.0f,
                                           static_cast<float>(noise_std));

        // Apply the carrier offset in the streaming loop (not baked into `full`)
        // so the phase stays continuous across the loop wrap.
        const double car_delta =
            2.0 * kPi * cfg_.mock_hrit_carrier_offset_hz / fs;
        double car_phase = 0.0;

        std::vector<std::complex<float>> buf(kBlock);
        const auto period = std::chrono::nanoseconds(
            static_cast<long long>(1e9 * static_cast<double>(kBlock) / fs));
        std::size_t pos = 0;
        while (running_.load(std::memory_order_relaxed)) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kBlock; ++i) {
                const std::complex<float> rot(
                    static_cast<float>(std::cos(car_phase)),
                    static_cast<float>(std::sin(car_phase)));
                buf[i] = full[pos] * rot +
                         std::complex<float>(nd(rng), nd(rng));
                car_phase += car_delta;
                if (car_phase > 2.0 * kPi) {
                    car_phase -= 2.0 * kPi;
                } else if (car_phase < -2.0 * kPi) {
                    car_phase += 2.0 * kPi;
                }
                pos = (pos + 1) % full.size();
            }
            samples_.fetch_add(kBlock, std::memory_order_relaxed);
            if (sink_) {
                sink_(buf.data(), kBlock);
            }
            const auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
    }

    SdrConfig cfg_;
    IqSink sink_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> samples_{0};
    std::atomic<uint64_t> overflows_{0};
    std::atomic<uint64_t> center_{0};
    std::atomic<uint32_t> rate_{0};
    std::atomic<int> gain_{0};
    std::atomic<bool> bias_{false};
    std::string last_error_;
};

std::unique_ptr<ISdrSource> make_mock() {
    return std::make_unique<MockSource>();
}

std::vector<SdrInfo> probe_mock() {
    MockSource source;
    return {source.info()};
}

}  // namespace ed::sdr
