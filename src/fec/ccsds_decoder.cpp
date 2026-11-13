#include "fec/ccsds_decoder.h"

#include <cstdio>
#include <cstdlib>

namespace ed::fec {

void CcsdsDecoder::configure(const FecConfig& cfg) {
    cfg_ = cfg;
    cadu_bytes_ = static_cast<std::size_t>(cfg_.cadu_bits) / 8;

    viterbi_.configure();
    viterbi_.set_lock_threshold(cfg_.viterbi_ber_threshold,
                                cfg_.viterbi_outsync_after);
    deframer_.configure(static_cast<std::size_t>(cfg_.cadu_bits));
    rs_.configure(cfg_.rs_dual_basis);
    reset();
}

void CcsdsDecoder::reset() {
    viterbi_.reset();
    deframer_.reset();
    bits_.clear();
    cadus_.clear();
    cadus_total_ = 0;
    cadus_uncorrectable_ = 0;
    rs_corrected_ = 0;
}

bool CcsdsDecoder::correct_cadu(uint8_t* cadu) {
    ++cadus_total_;
    if (cfg_.derandomize && cadu_bytes_ > 4) {
        derandomizer_.apply(cadu + 4, cadu_bytes_ - 4);
    }
    if (!cfg_.rs_enabled || cfg_.rs_interleave < 1 || cadu_bytes_ <= 4) {
        return true;
    }
    const std::size_t rs_len = cadu_bytes_ - 4;
    if (rs_len < static_cast<std::size_t>(cfg_.rs_interleave) *
                     ReedSolomon::kBlockLength) {
        return true;
    }
    std::vector<int> errors;
    rs_.decode_interleaved(cadu + 4, rs_len, cfg_.rs_interleave, errors);
    bool ok = true;
    int corrected = 0;
    for (int e : errors) {
        if (e < 0) {
            ok = false;
        } else if (e > 0) {
            corrected += e;
        }
    }
    rs_corrected_ += static_cast<uint64_t>(corrected);
    if (!ok) {
        ++cadus_uncorrectable_;
    }
    return ok;
}

void CcsdsDecoder::push_soft(const int8_t* soft, std::size_t n,
                             std::vector<std::vector<uint8_t>>& out) {
    if (soft == nullptr || n == 0) {
        return;
    }
    bits_.clear();
    viterbi_.push(soft, n, bits_);
    if (bits_.empty()) {
        return;
    }
    // Debug hook: dump the Viterbi output so framing can be checked against an
    // independent reference without instrumenting the deframer.
    {
        static FILE* vdump = nullptr;
        static bool checked = false;
        if (!checked) {
            checked = true;
            const char* path = std::getenv("ED_DUMP_VBITS");
            if (path != nullptr) {
                vdump = std::fopen(path, "wb");
            }
        }
        if (vdump != nullptr) {
            for (uint8_t bit : bits_) {
                std::fputc(bit ? '1' : '0', vdump);
            }
        }
    }
    cadus_.clear();
    deframer_.push(bits_.data(), bits_.size(), cadus_);
    for (std::vector<uint8_t>& cadu : cadus_) {
        correct_cadu(cadu.data());
        out.push_back(cadu);
    }
}

DecoderStats CcsdsDecoder::stats() const {
    DecoderStats s;
    s.ber = viterbi_.ber();
    s.viterbi_locked = viterbi_.locked();
    s.deframer_locked = deframer_.locked();
    s.cadus_total = cadus_total_;
    s.cadus_uncorrectable = cadus_uncorrectable_;
    s.rs_corrected = rs_corrected_;
    return s;
}

}  // namespace ed::fec
