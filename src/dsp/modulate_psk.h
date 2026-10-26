// ElektroDump — PSK modulator helpers (used by the mock SDR and tests).
// RRC-shaped BPSK/OQPSK baseband matching the demod's matched filter so the
// synthetic link is Nyquist; supports fractional samples/symbol (LRIT 3.40,
// HRIT 5.217).
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>

namespace ed::dsp {

std::size_t modulated_bpsk_samples(std::size_t nbits, double sps);

// Generate RRC-shaped BPSK IQ; `out` must hold modulated_bpsk_samples().
// bit=1 -> +1, bit=0 -> -1; the signal is rotated by `carrier_phase`.
void modulate_bpsk(const uint8_t* bits, std::size_t nbits, double sps,
                   double alpha, int taps, double carrier_phase,
                   std::complex<float>* out, std::size_t nout);

std::size_t modulated_oqpsk_samples(std::size_t nbits, double sps);

// Generate RRC-shaped OQPSK IQ. Bits are consumed in pairs: bit[2k] -> I rail,
// bit[2k+1] -> Q rail, Q delayed by half a symbol. OqpskAligner reverses this.
void modulate_oqpsk(const uint8_t* bits, std::size_t nbits, double sps,
                    double alpha, int taps, double carrier_phase,
                    std::complex<float>* out, std::size_t nout);

}  // namespace ed::dsp
