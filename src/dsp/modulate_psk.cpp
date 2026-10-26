#include "dsp/modulate_psk.h"

#include <cmath>

#include "dsp/rrc_filter.h"

namespace ed::dsp {

std::size_t modulated_bpsk_samples(std::size_t nbits, double sps) {
    if (nbits == 0 || !(sps > 0.0)) {
        return 0;
    }
    return static_cast<std::size_t>(static_cast<double>(nbits) * sps);
}

void modulate_bpsk(const uint8_t* bits, std::size_t nbits, double sps,
                   double alpha, int taps, double carrier_phase,
                   std::complex<float>* out, std::size_t nout) {
    if (bits == nullptr || out == nullptr || nbits == 0 || !(sps > 0.0)) {
        return;
    }
    for (std::size_t i = 0; i < nout; ++i) {
        out[i] = std::complex<float>(0.0f, 0.0f);
    }
    const double half_span = (taps > 0) ? (0.5 * taps) / sps : 6.0;
    const std::complex<float> carrier(
        static_cast<float>(std::cos(carrier_phase)),
        static_cast<float>(std::sin(carrier_phase)));

    for (std::size_t k = 0; k < nbits; ++k) {
        const double center = static_cast<double>(k) * sps;
        const double amp = bits[k] ? 1.0 : -1.0;
        const double lo = center - half_span;
        const double hi = center + half_span;
        long i0 = static_cast<long>(std::ceil(lo));
        const long i1 = static_cast<long>(std::floor(hi));
        if (i0 < 0) {
            i0 = 0;
        }
        for (long i = i0; i <= i1 && static_cast<std::size_t>(i) < nout; ++i) {
            const double t = static_cast<double>(i) / sps -
                             static_cast<double>(k);
            const double h = rrc_impulse(t, alpha);
            const float value =
                static_cast<float>(amp * h) * static_cast<float>(std::sqrt(sps));
            out[static_cast<std::size_t>(i)] += carrier * value;
        }
    }
}

std::size_t modulated_oqpsk_samples(std::size_t nbits, double sps) {
    if (nbits < 2 || !(sps > 0.0)) {
        return 0;
    }
    const std::size_t nsym = nbits / 2;
    // Last Q pulse is centred at (nsym-1)*sps + sps/2; add a symbol of margin.
    return static_cast<std::size_t>(static_cast<double>(nsym) * sps + sps + 1.0);
}

void modulate_oqpsk(const uint8_t* bits, std::size_t nbits, double sps,
                    double alpha, int taps, double carrier_phase,
                    std::complex<float>* out, std::size_t nout) {
    if (bits == nullptr || out == nullptr || nbits < 2 || !(sps > 0.0)) {
        return;
    }
    for (std::size_t i = 0; i < nout; ++i) {
        out[i] = std::complex<float>(0.0f, 0.0f);
    }
    const double half_span = (taps > 0) ? (0.5 * taps) / sps : 6.0;
    const double amp_scale = std::sqrt(sps);
    const std::size_t nsym = nbits / 2;

    for (std::size_t k = 0; k < nsym; ++k) {
        const double i_amp = bits[2 * k] ? 1.0 : -1.0;
        const double q_amp = bits[2 * k + 1] ? 1.0 : -1.0;
        const double i_center = static_cast<double>(k) * sps;
        const double q_center = i_center + 0.5 * sps;

        long i0 = static_cast<long>(std::ceil(i_center - half_span));
        const long i1 = static_cast<long>(std::floor(i_center + half_span));
        if (i0 < 0) {
            i0 = 0;
        }
        for (long i = i0; i <= i1 && static_cast<std::size_t>(i) < nout; ++i) {
            const double t = static_cast<double>(i) / sps -
                             static_cast<double>(k);
            const double h = rrc_impulse(t, alpha);
            out[static_cast<std::size_t>(i)] += std::complex<float>(
                static_cast<float>(i_amp * h * amp_scale), 0.0f);
        }

        // Q rail pulse, delayed by half a symbol.
        long q0 = static_cast<long>(std::ceil(q_center - half_span));
        const long q1 = static_cast<long>(std::floor(q_center + half_span));
        if (q0 < 0) {
            q0 = 0;
        }
        for (long i = q0; i <= q1 && static_cast<std::size_t>(i) < nout; ++i) {
            const double t = static_cast<double>(i) / sps -
                             (static_cast<double>(k) + 0.5);
            const double h = rrc_impulse(t, alpha);
            out[static_cast<std::size_t>(i)] += std::complex<float>(
                0.0f, static_cast<float>(q_amp * h * amp_scale));
        }
    }

    // Rotate the whole baseband by the carrier phase.
    const std::complex<float> carrier(
        static_cast<float>(std::cos(carrier_phase)),
        static_cast<float>(std::sin(carrier_phase)));
    for (std::size_t i = 0; i < nout; ++i) {
        out[i] = carrier * out[i];
    }
}

}  // namespace ed::dsp
