// ElektroDump — synthetic LRIT capture generator (hardware-free testing).
// Builds a valid xRIT image-segment file, frames it into CCSDS packets, wraps
// them in VCDUs/RS(255,223), and produces both post-deframer CADUs (offline
// `decode`) and the convolutionally-encoded bit stream (mock SDR / demod).
#pragma once

#include <cstdint>
#include <vector>

namespace ed::xrit {

struct SynthLrit {
    std::vector<uint8_t> file;                     // the assembled xRIT file
    std::vector<std::vector<uint8_t>> wire_cadus;  // 1024-byte CADUs (randomised)
    std::vector<uint8_t> tx_bits;                  // Viterbi-encoded bit stream
    uint16_t apid = 100;
    int channel = 1;
};

// data_bytes controls the file size (and hence the number of CADUs).
SynthLrit make_synthetic_lrit(uint16_t apid = 100, int channel = 1,
                              std::size_t data_bytes = 4000);

}  // namespace ed::xrit
