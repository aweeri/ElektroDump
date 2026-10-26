// ElektroDump — composed CCSDS FEC decoder (PLAN.md D3.2).
// Soft symbols -> Viterbi (rate 1/2) -> deframer -> derandomiser -> Reed-Solomon,
// emitting validated 1024-byte CADUs. Each stage is a separate class; this file
// only wires them together.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fec/deframer_ccsds.h"
#include "fec/derandomizer.h"
#include "fec/reed_solomon.h"
#include "fec/viterbi.h"

namespace ed::fec {

struct FecConfig {
    int cadu_bits = 8192;
    bool derandomize = true;
    int rs_interleave = 4;
    bool rs_enabled = true;
    bool rs_usecheck = true;
    bool rs_dual_basis = false;
    double viterbi_ber_threshold = 0.300;
    int viterbi_outsync_after = 20;
    bool nrzm = false;
};

struct DecoderStats {
    double ber = 0.0;
    bool viterbi_locked = false;
    bool deframer_locked = false;
    uint64_t cadus_total = 0;
    uint64_t cadus_uncorrectable = 0;
    uint64_t rs_corrected = 0;
};

class CcsdsDecoder {
public:
    void configure(const FecConfig& cfg);
    void reset();

    // Feed soft symbols; appends each derandomised, RS-corrected CADU to `out`.
    void push_soft(const int8_t* soft, std::size_t n,
                   std::vector<std::vector<uint8_t>>& out);

    // Apply derandomise + RS to an already-deframed CADU in place; true when
    // clean or fully corrected.
    bool correct_cadu(uint8_t* cadu);

    DecoderStats stats() const;
    std::size_t cadu_bytes() const { return cadu_bytes_; }

private:
    FecConfig cfg_;
    Viterbi viterbi_;
    CcsdsDeframer deframer_;
    Derandomizer derandomizer_;
    ReedSolomon rs_;

    std::size_t cadu_bytes_ = 1024;
    std::vector<uint8_t> bits_;
    std::vector<std::vector<uint8_t>> cadus_;

    uint64_t cadus_total_ = 0;
    uint64_t cadus_uncorrectable_ = 0;
    uint64_t rs_corrected_ = 0;
};

}  // namespace ed::fec
