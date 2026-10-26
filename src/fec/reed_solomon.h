// ElektroDump — Reed-Solomon rs223, interleave 4 (PLAN.md D3.7, digest §4.4).
// GF(256), primitive poly 0x187; CCSDS (255,223) with 32 parity symbols and
// generator roots alpha^(11*(112+j)). Decode: syndromes + Berlekamp-Massey +
// Chien search, error magnitudes solved as a Vandermonde system (no Forney).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ed::fec {

class ReedSolomon {
public:
    static constexpr int kBlockLength = 255;
    static constexpr int kMessageLength = 223;
    static constexpr int kParityLength = 32;

    // Converts codewords between the CCSDS dual-basis wire representation and
    // the polynomial basis; disabled by default (transform unvalidated).
    void configure(bool dual_basis = false);

    void encode_block(uint8_t* block) const;

    // Returns corrected symbol count (0 = clean) or -1 when uncorrectable.
    int decode_block(uint8_t* block) const;

    // Interleaved variants over `interleave` codewords (len == interleave*255).
    void encode_interleaved(uint8_t* data, std::size_t len,
                            int interleave) const;
    void decode_interleaved(uint8_t* data, std::size_t len, int interleave,
                            std::vector<int>& errors) const;

    bool dual_basis() const { return dual_basis_; }

private:
    bool dual_basis_ = false;
};

// Exposed for tests: the derived generator polynomial (ascending, 33 entries).
std::vector<uint8_t> rs_generator_ascending();

// Exposed for tests: dual-basis transform tables (256 entries each).
void rs_dual_basis_tables(uint8_t* to_dual, uint8_t* from_dual);

}  // namespace ed::fec
