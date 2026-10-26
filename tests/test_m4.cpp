// ElektroDump — M4 self-tests (LRIT chain: FEC, CCSDS, xRIT).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ccsds/aos_demux.h"
#include "ccsds/vcdu.h"
#include "dsp/demod_psk.h"
#include "dsp/modulate_psk.h"
#include "fec/ccsds_decoder.h"
#include "fec/crc16.h"
#include "fec/deframer_ccsds.h"
#include "fec/derandomizer.h"
#include "fec/reed_solomon.h"
#include "fec/viterbi.h"
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

void test_crc16() {
    const char* text = "123456789";
    const uint16_t crc = ed::fec::crc16_ccitt(
        reinterpret_cast<const uint8_t*>(text), 9);
    CHECK(crc == 0x29B1);
}

void test_derandomizer() {
    ed::fec::Derandomizer derand;
    const uint8_t expected[8] = {0xff, 0x48, 0x0e, 0xc0,
                                 0x9a, 0x0d, 0x70, 0xbc};
    for (int i = 0; i < 8; ++i) {
        CHECK(derand.table()[static_cast<std::size_t>(i)] == expected[i]);
    }
    std::vector<uint8_t> data(600);
    std::mt19937 rng(1234);
    for (uint8_t& b : data) {
        b = static_cast<uint8_t>(rng() & 0xFF);
    }
    const std::vector<uint8_t> original = data;
    derand.apply(data.data(), data.size());
    derand.apply(data.data(), data.size());
    CHECK(data == original);
}

void test_viterbi() {
    std::mt19937 rng(4321);
    const std::size_t nbits = 2000;
    std::vector<uint8_t> bits(nbits);
    for (uint8_t& b : bits) {
        b = static_cast<uint8_t>(rng() & 1);
    }
    std::vector<uint8_t> encoded;
    ed::fec::Viterbi::encode(bits.data(), bits.size(), 0171, 0133, encoded);
    CHECK(encoded.size() == nbits * 2);

    // Convert to soft symbols and flip a handful.
    std::vector<int8_t> soft(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        soft[i] = encoded[i] ? 100 : -100;
    }
    for (int k = 0; k < 8; ++k) {
        soft[static_cast<std::size_t>(rng() % soft.size())] *= -1;
    }

    ed::fec::Viterbi vit;
    vit.set_lock_threshold(0.30, 20);
    std::vector<uint8_t> decoded;
    vit.push(soft.data(), soft.size(), decoded);

    // Compare after a convergence prefix.
    const std::size_t skip = 128;
    std::size_t errors = 0;
    std::size_t total = 0;
    for (std::size_t i = skip; i < decoded.size() && i < bits.size(); ++i) {
        if (decoded[i] != bits[i]) {
            ++errors;
        }
        ++total;
    }
    std::printf("  viterbi: %zu bits, %zu errors\n", total, errors);
    CHECK(total > 1000);
    CHECK(errors < total / 100);
    CHECK(vit.locked());
}

void test_deframer() {
    ed::fec::CcsdsDeframer deframer;
    deframer.configure(8192, 0x1ACFFC1D);

    const uint32_t asm_word = 0x1ACFFC1D;
    std::vector<uint8_t> stream;
    // Leading junk.
    for (int i = 0; i < 37; ++i) {
        stream.push_back(static_cast<uint8_t>(i & 1));
    }
    const int cadus = 13;
    for (int c = 0; c < cadus; ++c) {
        for (int b = 31; b >= 0; --b) {
            stream.push_back(static_cast<uint8_t>((asm_word >> b) & 1));
        }
        for (int i = 0; i < 8160; ++i) {
            stream.push_back(static_cast<uint8_t>((i + c) & 1));
        }
    }
    std::vector<std::vector<uint8_t>> out;
    deframer.push(stream.data(), stream.size(), out);

    std::printf("  deframer: %zu cadus, locked=%d\n", out.size(),
                deframer.locked() ? 1 : 0);
    CHECK(out.size() >= 10);
    CHECK(deframer.locked());
    for (const auto& cadu : out) {
        CHECK(cadu.size() == 1024);
        CHECK(cadu[0] == 0x1A && cadu[1] == 0xCF && cadu[2] == 0xFC &&
              cadu[3] == 0x1D);
    }
}

void test_reed_solomon() {
    ed::fec::ReedSolomon rs;
    rs.configure(false);

    std::mt19937 rng(99);
    std::vector<uint8_t> block(255);
    for (int i = 0; i < 223; ++i) {
        block[static_cast<std::size_t>(i)] =
            static_cast<uint8_t>(rng() & 0xFF);
    }
    const std::vector<uint8_t> message(block.begin(), block.begin() + 223);
    rs.encode_block(block.data());

    // Clean decode.
    std::vector<uint8_t> clean = block;
    CHECK(rs.decode_block(clean.data()) == 0);
    CHECK(std::equal(clean.begin(), clean.begin() + 223, message.begin()));

    // Corrupt up to 16 symbols and correct.
    std::vector<uint8_t> noisy = block;
    for (int k = 0; k < 16; ++k) {
        noisy[static_cast<std::size_t>(rng() % 255)] ^=
            static_cast<uint8_t>(1 + (rng() % 255));
    }
    const std::vector<uint8_t> noisy_copy = noisy;
    const int corrected = rs.decode_block(noisy.data());
    std::printf("  rs: corrected=%d\n", corrected);
    CHECK(corrected > 0);
    CHECK(std::equal(noisy.begin(), noisy.begin() + 223, message.begin()));

    // Interleaved round-trip.
    ed::fec::ReedSolomon irs;
    irs.configure(false);
    std::vector<uint8_t> data(1020);
    for (uint8_t& b : data) {
        b = static_cast<uint8_t>(rng() & 0xFF);
    }
    const std::vector<uint8_t> data_copy = data;
    irs.encode_interleaved(data.data(), data.size(), 4);
    std::vector<int> errors;
    irs.decode_interleaved(data.data(), data.size(), 4, errors);
    CHECK(std::equal(data.begin(), data.begin() + 892, data_copy.begin()));
}

void test_ccsds_decoder_chain() {
    // Build one CADU (ASM + payload), RS-encode + derandomise, Viterbi-encode,
    // then decode through the full FEC chain.
    ed::fec::FecConfig cfg;
    cfg.rs_dual_basis = false;
    ed::fec::CcsdsDecoder decoder;
    decoder.configure(cfg);

    std::mt19937 rng(7);
    std::vector<uint8_t> cadu(1024);
    cadu[0] = 0x1A; cadu[1] = 0xCF; cadu[2] = 0xFC; cadu[3] = 0x1D;
    for (std::size_t i = 4; i < cadu.size(); ++i) {
        cadu[i] = static_cast<uint8_t>(rng() & 0xFF);
    }
    // RS-encode the body region.
    ed::fec::ReedSolomon rs;
    rs.configure(false);
    rs.encode_interleaved(cadu.data() + 4, 1020, 4);
    // Derandomise (same operation as de-randomise).
    ed::fec::Derandomizer derand;
    derand.apply(cadu.data() + 4, 1020);
    // Viterbi-encode the whole CADU.
    std::vector<uint8_t> bits(cadu.size() * 8);
    for (std::size_t i = 0; i < cadu.size(); ++i) {
        for (int b = 0; b < 8; ++b) {
            bits[i * 8 + static_cast<std::size_t>(b)] =
                static_cast<uint8_t>((cadu[i] >> (7 - b)) & 1);
        }
    }
    std::vector<uint8_t> encoded;
    ed::fec::Viterbi::encode(bits.data(), bits.size(), 0171, 0133, encoded);
    std::vector<int8_t> soft(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        soft[i] = encoded[i] ? 90 : -90;
    }
    // Feed the same CADU several times so the deframer can lock.
    std::vector<int8_t> stream;
    for (int r = 0; r < 13; ++r) {
        stream.insert(stream.end(), soft.begin(), soft.end());
    }

    std::vector<std::vector<uint8_t>> out;
    for (std::size_t pos = 0; pos < stream.size(); pos += 8192) {
        const std::size_t n = std::min<std::size_t>(8192, stream.size() - pos);
        decoder.push_soft(stream.data() + pos, n, out);
    }
    const ed::fec::DecoderStats stats = decoder.stats();
    std::printf("  chain: cadus=%zu ber=%.4f vit=%d def=%d\n", out.size(),
                stats.ber, stats.viterbi_locked ? 1 : 0,
                stats.deframer_locked ? 1 : 0);
    CHECK(out.size() >= 5);
    CHECK(stats.deframer_locked);
}

void test_xrit_demux_wire() {
    // Feed the randomised post-deframer CADUs through derandomise+RS+xRIT.
    const ed::xrit::SynthLrit synth =
        ed::xrit::make_synthetic_lrit(250, 3, 3000);

    ed::fec::FecConfig cfg;
    cfg.rs_dual_basis = false;
    ed::fec::CcsdsDecoder decoder;
    decoder.configure(cfg);
    ed::xrit::XritDemux demux;
    std::vector<ed::xrit::XritFile> files;

    for (const auto& wire : synth.wire_cadus) {
        std::vector<uint8_t> cadu = wire;
        decoder.correct_cadu(cadu.data());
        demux.push_cadu(cadu.data(), cadu.size(), files);
    }
    std::printf("  xrit wire: %zu cadus, %zu file(s)\n", synth.wire_cadus.size(),
                files.size());
    CHECK(files.size() == 1);
    if (files.size() == 1) {
        CHECK(files[0].header_parsed);
        CHECK(files[0].filename == "SYNTH_3.lrit");
        CHECK(files[0].data.size() >= synth.file.size());
        CHECK(std::equal(synth.file.begin(), synth.file.end(),
                         files[0].data.begin()));
    }
}

void test_xrit_demux_fec() {
    // Feed the Viterbi-encoded bit stream through the full FEC chain.
    const ed::xrit::SynthLrit synth =
        ed::xrit::make_synthetic_lrit(251, 5, 5000);

    ed::fec::FecConfig cfg;
    ed::fec::CcsdsDecoder decoder;
    decoder.configure(cfg);
    ed::xrit::XritDemux demux;
    std::vector<ed::xrit::XritFile> files;

    std::vector<int8_t> soft;
    for (int r = 0; r < 3; ++r) {
        for (std::size_t i = 0; i < synth.tx_bits.size(); ++i) {
            soft.push_back(synth.tx_bits[i] ? 90 : -90);
        }
    }
    std::vector<std::vector<uint8_t>> cadus;
    for (std::size_t pos = 0; pos < soft.size(); pos += 4096) {
        const std::size_t nn =
            std::min<std::size_t>(4096, soft.size() - pos);
        decoder.push_soft(soft.data() + pos, nn, cadus);
    }
    for (auto& cadu : cadus) {
        demux.push_cadu(cadu.data(), cadu.size(), files);
    }
    bool found = false;
    for (const auto& f : files) {
        if (f.data.size() >= synth.file.size() &&
            std::equal(synth.file.begin(), synth.file.end(), f.data.begin())) {
            found = true;
        }
    }
    std::printf("  xrit fec: %zu cadus, %zu files\n", cadus.size(),
                files.size());
    CHECK(files.size() >= 1);
    CHECK(found);
}

void test_demod_bpsk() {
    // Clean RRC-shaped BPSK with a carrier phase offset: the demod must
    // recover the transmitted bits (up to a lag and a 180-degree ambiguity,
    // both handled downstream by the deframer's ASM search).
    const double fs = 1000000.0;
    const double baud = 294000.0;
    const double sps = fs / baud;
    std::mt19937 rng(11);
    std::vector<uint8_t> bits(4000);
    for (uint8_t& b : bits) {
        b = static_cast<uint8_t>(rng() & 1);
    }
    const std::size_t total = ed::dsp::modulated_bpsk_samples(bits.size(), sps);
    std::vector<std::complex<float>> iq(total);
    ed::dsp::modulate_bpsk(bits.data(), bits.size(), sps, 0.5, 33, 0.4,
                           iq.data(), iq.size());
    ed::dsp::DemodConfig dcfg;
    dcfg.symbol_rate = baud;
    dcfg.pll_bw = 0.0145;
    ed::dsp::PskDemod demod;
    demod.configure(dcfg, static_cast<uint32_t>(fs));
    std::vector<int8_t> soft;
    demod.process(iq.data(), iq.size(), soft);

    int best = 0;
    int count = 0;
    for (int inv = 0; inv < 2; ++inv) {
        for (int lag = 0; lag < 60; ++lag) {
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
    std::printf("  demod bpsk: soft=%zu best_match=%d/%d snr=%.1f\n",
                soft.size(), best, count, demod.snr_db());
    CHECK(count > 3000);
    CHECK(best * 100 >= count * 95);
    CHECK(demod.snr_db() > 8.0);
}

void test_demod_chain() {
    const ed::xrit::SynthLrit synth =
        ed::xrit::make_synthetic_lrit(253, 9, 5000);

    const double fs = 1000000.0;
    const double baud = 294000.0;
    const double sps = fs / baud;
    const int repeats = 4;

    std::vector<uint8_t> bit_stream;
    for (int r = 0; r < repeats; ++r) {
        bit_stream.insert(bit_stream.end(), synth.tx_bits.begin(),
                          synth.tx_bits.end());
    }

    const std::size_t total =
        ed::dsp::modulated_bpsk_samples(bit_stream.size(), sps);
    std::mt19937 rng(2025);
    std::normal_distribution<float> nd(0.0f, 0.07f);
    std::vector<std::complex<float>> iq(total);
    ed::dsp::modulate_bpsk(bit_stream.data(), bit_stream.size(), sps, 0.5, 33,
                           0.4, iq.data(), iq.size());
    for (std::complex<float>& sample : iq) {
        sample += std::complex<float>(nd(rng), nd(rng));
    }

    ed::dsp::DemodConfig dcfg;
    dcfg.constellation = ed::dsp::Constellation::BPSK;
    dcfg.symbol_rate = baud;
    dcfg.rrc_alpha = 0.5;
    dcfg.pll_bw = 0.0145;
    ed::dsp::PskDemod demod;
    demod.configure(dcfg, static_cast<uint32_t>(fs));

    ed::fec::FecConfig cfg;
    ed::fec::CcsdsDecoder decoder;
    decoder.configure(cfg);
    ed::xrit::XritDemux demux;
    std::vector<ed::xrit::XritFile> files;

    std::vector<int8_t> soft;
    std::vector<std::vector<uint8_t>> cadus;
    const std::size_t block = 8192;
    for (std::size_t pos = 0; pos < iq.size(); pos += block) {
        const std::size_t nn = std::min(block, iq.size() - pos);
        soft.clear();
        demod.process(iq.data() + pos, nn, soft);
        decoder.push_soft(soft.data(), soft.size(), cadus);
    }
    for (auto& cadu : cadus) {
        demux.push_cadu(cadu.data(), cadu.size(), files);
    }
    const ed::fec::DecoderStats stats = decoder.stats();
    std::printf("  demod: snr=%.1f cadus=%zu files=%zu locked=%d\n",
                demod.snr_db(), cadus.size(), files.size(),
                stats.deframer_locked ? 1 : 0);
    bool found = false;
    for (const auto& f : files) {
        if (f.data.size() >= synth.file.size() &&
            std::equal(synth.file.begin(), synth.file.end(), f.data.begin())) {
            found = true;
        }
    }
    CHECK(demod.snr_db() > 3.0);
    CHECK(files.size() >= 1);
    CHECK(found);
}

// Write a synthetic CADU fixture + the expected assembled file, so the
// hardware-free smoke script can drive the real `elektrodump decode` binary.
void write_fixture() {
    const char* dir = "build/m4_fixture";
    ed::fs::mkdir_p(dir);
    const ed::xrit::SynthLrit synth =
        ed::xrit::make_synthetic_lrit(250, 3, 3000);
    std::string cadus;
    for (const auto& wire : synth.wire_cadus) {
        cadus.append(reinterpret_cast<const char*>(wire.data()), wire.size());
    }
    ed::fs::write_file(std::string(dir) + "/capture.cadu", cadus);
    ed::fs::write_file(
        std::string(dir) + "/expected.lrit",
        std::string(synth.file.begin(), synth.file.end()));
    std::printf("fixture: %s (%zu CADU, expected %zu B)\n", dir,
                synth.wire_cadus.size(), synth.file.size());
}

}  // namespace

int main() {
    write_fixture();
    test_crc16();
    test_derandomizer();
    test_viterbi();
    test_deframer();
    test_reed_solomon();
    test_ccsds_decoder_chain();
    test_xrit_demux_wire();
    test_xrit_demux_fec();
    test_demod_bpsk();
    test_demod_chain();

    if (g_failures == 0) {
        std::printf("all M4 tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
