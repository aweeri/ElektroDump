// ElektroDump — M5 self-tests (HRIT chain: OQPSK demod, coarse acquisition).
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "dsp/coarse_acq.h"
#include "dsp/demod_psk.h"
#include "dsp/modulate_psk.h"
#include "dsp/oqpsk_align.h"
#include "fec/ccsds_decoder.h"
#include "util/fs_util.h"
#include "xrit/synth.h"
#include "xrit/xrit_demux.h"
#include "xrit/xrit_file.h"

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

// Apply a constant carrier frequency offset to a block of IQ.
void apply_offset(std::vector<std::complex<float>>& iq, double offset_hz,
                  double fs) {
    const double dphi = 2.0 * kPi * offset_hz / fs;
    double phase = 0.0;
    for (std::complex<float>& s : iq) {
        s *= std::complex<float>(static_cast<float>(std::cos(phase)),
                                 static_cast<float>(std::sin(phase)));
        phase += dphi;
        if (phase > 2.0 * kPi) {
            phase -= 2.0 * kPi;
        } else if (phase < -2.0 * kPi) {
            phase += 2.0 * kPi;
        }
    }
}

void add_noise(std::vector<std::complex<float>>& iq, double stddev,
               std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, static_cast<float>(stddev));
    for (std::complex<float>& s : iq) {
        s += std::complex<float>(nd(rng), nd(rng));
    }
}

// --- OQPSK aligner ---------------------------------------------------------

void test_oqpsk_aligner() {
    // A ramp on the I rail must come out delayed by T/2 samples.
    const double sps = 4.0;
    ed::dsp::OqpskAligner aligner;
    aligner.configure(sps);
    CHECK(std::fabs(aligner.delay_samples() - 2.0) < 1e-9);

    std::vector<std::complex<float>> in(32);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::complex<float>(static_cast<float>(i), 0.0f);
    }
    std::vector<std::complex<float>> out(in.size());
    aligner.process(in.data(), in.size(), out.data());
    // out[i] should equal in[i - 2] (integer delay here).
    bool ok = true;
    for (std::size_t i = 4; i < out.size(); ++i) {
        if (std::fabs(out[i].real() - static_cast<float>(i - 2)) > 1e-3f) {
            ok = false;
        }
    }
    std::printf("  oqpsk aligner: delay=%.2f ok=%d\n", aligner.delay_samples(),
                ok ? 1 : 0);
    CHECK(ok);
}

// --- Coarse carrier acquisition -------------------------------------------

void test_coarse_acq() {
    const double fs = 6000000.0;
    const double baud = 1150000.0;
    const double sps = fs / baud;

    std::mt19937 rng(5);
    std::vector<uint8_t> bits(20000);
    for (uint8_t& b : bits) {
        b = static_cast<uint8_t>(rng() & 1);
    }
    const std::size_t total = ed::dsp::modulated_oqpsk_samples(bits.size(), sps);
    std::vector<std::complex<float>> iq(total);
    ed::dsp::modulate_oqpsk(bits.data(), bits.size(), sps, 0.5, 33, 0.0,
                            iq.data(), iq.size());

    const double true_offset = 42000.0;
    apply_offset(iq, true_offset, fs);
    add_noise(iq, 0.05, 99);

    ed::dsp::CoarseAcquisition acq;
    acq.configure(fs, 100000.0, 16384, 4);
    const double est = acq.estimate(iq.data(), iq.size());
    std::printf("  coarse acq: true=%.0f est=%.0f ratio=%.1f\n", true_offset, est,
                acq.last_peak_ratio());
    CHECK(std::fabs(est - true_offset) < 2000.0);
}

// --- OQPSK demod -----------------------------------------------------------

// Demodulate an OQPSK burst and return the best bit match (over lag and
// inversion). `coarse` toggles the coarse acquisition stage; `manual` applies
// the offset correction in the test instead (to isolate the demod's own
// correction path).
void run_demod_oqpsk(double offset_hz, bool coarse, bool manual,
                     const char* label) {
    const double fs = 6000000.0;
    const double baud = 1150000.0;
    const double sps = fs / baud;

    std::mt19937 rng(11);
    std::vector<uint8_t> bits(40000);
    for (uint8_t& b : bits) {
        b = static_cast<uint8_t>(rng() & 1);
    }
    const std::size_t total = ed::dsp::modulated_oqpsk_samples(bits.size(), sps);
    std::vector<std::complex<float>> iq(total);
    ed::dsp::modulate_oqpsk(bits.data(), bits.size(), sps, 0.5, 33, 0.4,
                            iq.data(), iq.size());
    if (offset_hz != 0.0) {
        apply_offset(iq, offset_hz, fs);
    }
    add_noise(iq, 0.05, 7);

    if (manual && offset_hz != 0.0) {
        const double dphi = 2.0 * kPi * offset_hz / fs;
        double phase = 0.0;
        for (std::complex<float>& s : iq) {
            s *= std::complex<float>(static_cast<float>(std::cos(phase)),
                                     static_cast<float>(-std::sin(phase)));
            phase += dphi;
            if (phase > kPi) {
                phase -= 2.0 * kPi;
            }
        }
    }

    ed::dsp::DemodConfig dcfg;
    dcfg.constellation = ed::dsp::Constellation::OQPSK;
    dcfg.symbol_rate = baud;
    dcfg.rrc_alpha = 0.5;
    dcfg.pll_bw = 0.001;
    dcfg.coarse_acq = coarse;
    dcfg.coarse_acq_range_hz = 100000.0;
    dcfg.coarse_acq_fft = 16384;
    ed::dsp::PskDemod demod;
    demod.configure(dcfg, static_cast<uint32_t>(fs));

    std::vector<int8_t> soft;
    demod.process(iq.data(), iq.size(), soft);

    int best = 0;
    int count = 0;
    for (int inv = 0; inv < 2; ++inv) {
        for (int lag = 0; lag < 120; ++lag) {
            int match = 0;
            int total_cmp = 0;
            for (std::size_t i = 0;
                 i + static_cast<std::size_t>(lag) < soft.size() &&
                 i < bits.size();
                 ++i) {
                const int predicted =
                    (bits[i] ^ static_cast<uint8_t>(inv)) ? 1 : 0;
                const int got =
                    soft[i + static_cast<std::size_t>(lag)] >= 0 ? 1 : 0;
                if (predicted == got) {
                    ++match;
                }
                ++total_cmp;
            }
            if (total_cmp > 0 && match > best) {
                best = match;
                count = total_cmp;
            }
        }
    }
    std::printf("  demod oqpsk[%s]: off=%.0f est=%.0f soft=%zu best=%d/%d "
                "snr=%.1f\n",
                label, offset_hz, demod.carrier_offset_hz(), soft.size(), best,
                count, demod.snr_db());
}

void test_demod_oqpsk() {
    run_demod_oqpsk(0.0, true, false, "no-offset");
    run_demod_oqpsk(25000.0, false, false, "off-noacq");
    run_demod_oqpsk(25000.0, true, false, "off-acq");
    run_demod_oqpsk(25000.0, false, true, "off-manual");
    run_demod_oqpsk(5000.0, true, false, "small-off-acq");
}

// --- Full HRIT chain (OQPSK -> FEC -> xRIT) --------------------------------

void test_hrit_chain() {
    const ed::xrit::SynthLrit synth =
        ed::xrit::make_synthetic_lrit(253, 9, 5000);

    const double fs = 6000000.0;
    const double baud = 1150000.0;
    const double sps = fs / baud;
    const int repeats = 4;

    std::vector<uint8_t> bit_stream;
    for (int r = 0; r < repeats; ++r) {
        bit_stream.insert(bit_stream.end(), synth.tx_bits.begin(),
                          synth.tx_bits.end());
    }

    const std::size_t total =
        ed::dsp::modulated_oqpsk_samples(bit_stream.size(), sps);
    std::vector<std::complex<float>> iq(total);
    ed::dsp::modulate_oqpsk(bit_stream.data(), bit_stream.size(), sps, 0.5, 33,
                            0.4, iq.data(), iq.size());
    apply_offset(iq, 25000.0, fs);
    add_noise(iq, 0.05, 2025);

    ed::dsp::DemodConfig dcfg;
    dcfg.constellation = ed::dsp::Constellation::OQPSK;
    dcfg.symbol_rate = baud;
    dcfg.rrc_alpha = 0.5;
    dcfg.pll_bw = 0.001;
    dcfg.coarse_acq = true;
    dcfg.coarse_acq_range_hz = 100000.0;
    dcfg.coarse_acq_fft = 16384;
    ed::dsp::PskDemod demod;
    demod.configure(dcfg, static_cast<uint32_t>(fs));

    ed::fec::FecConfig cfg;
    ed::fec::CcsdsDecoder decoder;
    decoder.configure(cfg);
    ed::xrit::XritDemux demux;
    std::vector<ed::xrit::XritFile> files;

    std::vector<int8_t> soft;
    std::vector<std::vector<uint8_t>> cadus;
    const std::size_t block = 16384;
    for (std::size_t pos = 0; pos < iq.size(); pos += block) {
        const std::size_t nn = std::min(block, iq.size() - pos);
        soft.clear();
        demod.process(iq.data() + pos, nn, soft);
        decoder.push_soft(soft.data(), soft.size(), cadus);
    }
    for (std::vector<uint8_t>& cadu : cadus) {
        demux.push_cadu(cadu.data(), cadu.size(), files);
    }

    const ed::fec::DecoderStats stats = decoder.stats();
    std::printf(
        "  hrit chain: snr=%.1f offset=%.0f cadus=%zu files=%zu locked=%d\n",
        demod.snr_db(), demod.carrier_offset_hz(), cadus.size(), files.size(),
        stats.deframer_locked ? 1 : 0);

    bool found = false;
    for (const ed::xrit::XritFile& f : files) {
        if (f.data.size() >= synth.file.size() &&
            std::equal(synth.file.begin(), synth.file.end(), f.data.begin())) {
            found = true;
        }
    }
    CHECK(demod.snr_db() > 3.0);
    CHECK(files.size() >= 1);
    CHECK(found);
}

// Write a synthetic HRIT IQ fixture + the expected assembled file, so the
// hardware-free smoke script can drive the real `elektrodump decode` binary.
void write_hrit_fixture() {
    const char* dir = "build/m5_fixture";
    ed::fs::mkdir_p(dir);

    const ed::xrit::SynthLrit synth =
        ed::xrit::make_synthetic_lrit(250, 3, 3000);
    const double fs = 6000000.0;
    const double baud = 1150000.0;
    const double sps = fs / baud;
    const int repeats = 4;

    std::vector<uint8_t> bit_stream;
    for (int r = 0; r < repeats; ++r) {
        bit_stream.insert(bit_stream.end(), synth.tx_bits.begin(),
                          synth.tx_bits.end());
    }
    const std::size_t total =
        ed::dsp::modulated_oqpsk_samples(bit_stream.size(), sps);
    std::vector<std::complex<float>> iq(total);
    ed::dsp::modulate_oqpsk(bit_stream.data(), bit_stream.size(), sps, 0.5, 33,
                            0.4, iq.data(), iq.size());
    apply_offset(iq, 25000.0, fs);
    add_noise(iq, 0.05, 7);

    ed::fs::write_file(
        std::string(dir) + "/capture.cf32",
        std::string(reinterpret_cast<const char*>(iq.data()),
                    iq.size() * sizeof(std::complex<float>)));
    ed::fs::write_file(std::string(dir) + "/expected.lrit",
                       std::string(synth.file.begin(), synth.file.end()));
    std::printf("hrit fixture: %s (%zu samples, expected %zu B)\n", dir,
                iq.size(), synth.file.size());
}

}  // namespace

int main() {
    write_hrit_fixture();
    test_oqpsk_aligner();
    test_coarse_acq();
    test_demod_oqpsk();
    test_hrit_chain();

    if (g_failures == 0) {
        std::printf("all M5 tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
