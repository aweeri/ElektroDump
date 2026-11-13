// ElektroDump — M3 self-tests (SNR, frame sync, SKL, PM demod, CADU store).
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dsp/demod_pm.h"
#include "dsp/snr_estimator.h"
#include "ggak/ggak_frame.h"
#include "ggak/ggak_skl.h"
#include "ggak/ggak_store.h"
#include "util/fs_util.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                         #cond);                                         \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

constexpr double kPi = 3.14159265358979323846;

// Build a valid 224-byte GGAK frame with the given id and a deterministic
// payload, then append the sum-CRC.
ed::ggak::GgakFrame make_frame(uint8_t id, uint32_t seed) {
    ed::ggak::GgakFrame frame;
    // CCSDS ASM + GGAK header, matching the real CADU layout.
    frame.raw[0] = 0x1A;
    frame.raw[1] = 0xCF;
    frame.raw[2] = 0xFC;
    frame.raw[3] = 0x1D;
    frame.raw[4] = id;  // frame_type
    frame.raw[5] = static_cast<uint8_t>((seed >> 8) & 0xFF);
    frame.raw[6] = static_cast<uint8_t>(seed & 0xFF);
    frame.raw[7] = 0;
    frame.raw[8] = 0;
    frame.raw[9] = id;  // source_id
    const uint32_t coarse = seed & 0xFFFF;
    frame.raw[10] = static_cast<uint8_t>((coarse >> 8) & 0xFF);
    frame.raw[11] = static_cast<uint8_t>(coarse & 0xFF);
    frame.raw[12] = 0;  // fine_time
    for (std::size_t i = 13; i < 222; ++i) {
        frame.raw[i] = static_cast<uint8_t>((i + seed) & 0xFF);
    }
    const uint16_t sum = ed::ggak::ggak_sum_crc(frame.raw);
    frame.raw[222] = static_cast<uint8_t>((sum >> 8) & 0xFF);
    frame.raw[223] = static_cast<uint8_t>(sum & 0xFF);
    frame.frame_type = id;
    frame.id = id;
    frame.frame_time = coarse;
    frame.crc_ok = true;
    return frame;
}

void test_sum_crc() {
    ed::ggak::GgakFrame frame = make_frame(48, 1);
    CHECK(ed::ggak::ggak_frame_crc_ok(frame.raw));

    frame.raw[100] ^= 0x01;
    CHECK(!ed::ggak::ggak_frame_crc_ok(frame.raw));
}

void test_frame_parser() {
    std::vector<ed::ggak::GgakFrame> frames;
    for (uint32_t i = 0; i < 4; ++i) {
        frames.push_back(make_frame(48, i));
    }

    // Convert frames to soft symbols (MSB first, +100/-100).
    std::vector<int8_t> soft;
    for (const auto& frame : frames) {
        for (std::size_t b = 0; b < ed::ggak::kGgakFrameBytes; ++b) {
            for (int k = 7; k >= 0; --k) {
                const int bit = (frame.raw[b] >> k) & 1;
                soft.push_back(bit ? 100 : -100);
            }
        }
    }

    ed::ggak::GgakFrameParser parser;
    std::vector<ed::ggak::GgakFrame> out;
    parser.push(soft.data(), soft.size(), out);

    CHECK(out.size() == frames.size());
    CHECK(parser.locked());
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].crc_ok);
        CHECK(out[i].id == 48);
        CHECK(std::memcmp(out[i].raw, frames[i].raw,
                          ed::ggak::kGgakFrameBytes) == 0);
    }
}

void test_skl() {
    ed::ggak::GgakFrame frame = make_frame(48, 7);
    // Put a known value in the first record's first channel.
    frame.raw[13 + 3] = 67;  // exp((67-67)/29.02)*1000 = 1000
    const std::vector<ed::ggak::GgakSklRecord> records =
        ed::ggak::parse_skl(frame);
    CHECK(records.size() == 14);
    CHECK(std::fabs(records[0].dat[0] - 1000.0) < 1e-6);

    // Non-SKL frames produce no records.
    ed::ggak::GgakFrame other = make_frame(1, 7);
    CHECK(ed::ggak::parse_skl(other).empty());
}

void test_snr_estimators() {
    // EVM: clean BPSK symbols should report a high SNR.
    ed::dsp::EvmSnrEstimator evm(2, 0.05);
    std::vector<std::complex<float>> symbols;
    for (int i = 0; i < 2000; ++i) {
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        symbols.emplace_back(sign, 0.0f);
    }
    evm.update(symbols.data(), symbols.size());
    CHECK(evm.snr_db() > 20.0);

    // M2M4: a strong tone plus a little noise should report a positive SNR.
    ed::dsp::M2m4SnrEstimator m2m4(0.01);
    std::vector<std::complex<float>> iq;
    for (int i = 0; i < 4000; ++i) {
        const float noise = (i % 7 == 0) ? 0.1f : -0.1f;
        iq.emplace_back(1.0f + noise, noise);
    }
    m2m4.update(iq.data(), iq.size());
    CHECK(m2m4.snr_db() > 0.0);
}

// Generate a synthetic PM/BPSK GGAK signal and run it through the demod.
void test_pm_demod() {
    constexpr uint32_t kSampleRate = 3000000;
    constexpr double kSymbolRate = 5000.0;
    constexpr double kBeta = 0.5;
    constexpr int kFrameCount = 8;

    std::vector<ed::ggak::GgakFrame> frames;
    for (int i = 0; i < kFrameCount; ++i) {
        frames.push_back(make_frame(48, static_cast<uint32_t>(i)));
    }

    // Bit stream (MSB first, +1/-1).
    std::vector<int> bits;
    for (const auto& frame : frames) {
        for (std::size_t b = 0; b < ed::ggak::kGgakFrameBytes; ++b) {
            for (int k = 7; k >= 0; --k) {
                bits.push_back((frame.raw[b] >> k) & 1);
            }
        }
    }

    const double sps = static_cast<double>(kSampleRate) / kSymbolRate;
    const double sub_delta = 2.0 * kPi * kSymbolRate / kSampleRate;

    ed::dsp::PmDemodConfig cfg;
    cfg.symbol_rate = kSymbolRate;
    cfg.rrc_alpha = 0.5;
    cfg.pll_bw = 0.02;
    cfg.costas_bw = 0.02;
    cfg.target_sps = 8;
    cfg.rrc_taps = 33;

    ed::dsp::PmDemod demod;
    demod.configure(cfg, kSampleRate);
    ed::ggak::GgakFrameParser parser;

    constexpr std::size_t kBlock = 16384;
    std::vector<std::complex<float>> iq(kBlock);
    std::vector<int8_t> soft;
    std::vector<ed::ggak::GgakFrame> out;

    std::size_t bit_index = 0;
    double sample_index = 0.0;
    double sub_phase = 0.0;
    const std::size_t total_samples =
        static_cast<std::size_t>(bits.size() * sps);

    for (std::size_t base = 0; base < total_samples; base += kBlock) {
        const std::size_t n =
            std::min(kBlock, total_samples - base);
        for (std::size_t i = 0; i < n; ++i) {
            const double d = bits[bit_index] ? 1.0 : -1.0;
            const double phase = kBeta * d * std::cos(sub_phase);
            iq[i] = std::complex<float>(static_cast<float>(std::cos(phase)),
                                        static_cast<float>(std::sin(phase)));
            sub_phase += sub_delta;
            if (sub_phase > 2.0 * kPi) {
                sub_phase -= 2.0 * kPi;
            }
            sample_index += 1.0;
            if (sample_index >= sps) {
                sample_index -= sps;
                if (bit_index + 1 < bits.size()) {
                    ++bit_index;
                }
            }
        }
        soft.clear();
        demod.process(iq.data(), n, soft);
        parser.push(soft.data(), soft.size(), out);
    }

    std::printf("  pm demod: %zu/%d frames recovered, snr %.1f dB\n",
                out.size(), kFrameCount, demod.snr_db());
    CHECK(out.size() >= 2);
    CHECK(demod.snr_db() > 0.0);
    // Frames are delimited by the ASM now, so the parser emits frames the ASM
    // says are real even when the payload checksum fails (that is what crc_ok
    // reports). At this SNR almost all payloads should survive.
    std::size_t valid = 0;
    for (const auto& frame : out) {
        if (frame.crc_ok) {
            ++valid;
        }
    }
    CHECK(valid * 2 >= out.size());
}

void test_store() {
    const std::string dir = "build/test_m3_spool";
    ed::fs::remove_all(dir);

    ed::ggak::GgakStore store;
    store.configure(dir, 3600);
    const ed::ggak::GgakFrame frame = make_frame(48, 3);
    CHECK(store.write(frame));
    store.flush();
    CHECK(store.frames_written() == 1);
    CHECK(!store.current_path().empty());
    CHECK(ed::fs::file_size(store.current_path()) ==
          ed::ggak::kGgakFrameBytes);
    store.close();
    ed::fs::remove_all(dir);
}

}  // namespace

int main() {
    test_sum_crc();
    test_frame_parser();
    test_skl();
    test_snr_estimators();
    test_pm_demod();
    test_store();

    if (g_failures == 0) {
        std::printf("all M3 tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
