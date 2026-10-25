/*
 * HackRF T2 Viewer - low-overhead pilot interpolation for data carriers.
 * GPL-3.0-or-later.
 */
#ifndef DVB_T2_FAST_PILOT_EQUALIZER_H
#define DVB_T2_FAST_PILOT_EQUALIZER_H

#include <cmath>
#include <complex>

// The original hot path evaluated sinf() and cosf() for *every data carrier*,
// millions of times per second at 10 MS/s. Across one interval between pilots
// the equalizer phase advances at a constant rate, so it can use one complex
// rotation per interval. This preserves phase unwrapping and linear amplitude
// interpolation, rather than changing the reception algorithm without an
// independent real-RF reference test.
//
// 'addresses' points at the permutation's next 'count' carrier indices.
// A near-zero or invalid channel estimate is erased instead of producing
// Inf/NaN LLRs in the downstream FEC decoder.
inline void fast_equalize_pilot_gap(
    const std::complex<float> *raw,
    std::complex<float> *out,
    const int *addresses,
    int count,
    float startAngle,
    float deltaAngle,
    float startAmplitude,
    float deltaAmplitude) noexcept
{
    if (count <= 0)
        return;

    const float firstAngle = startAngle + deltaAngle;
    float real = std::cos(firstAngle);
    float imag = -std::sin(firstAngle);
    const float stepReal = std::cos(deltaAngle);
    const float stepImag = -std::sin(deltaAngle);
    float amplitude = startAmplitude;

    for (int j = 0; j < count; ++j) {
        amplitude += deltaAmplitude;
        const float inverse = std::isfinite(amplitude) && amplitude > 1.0e-6f
            ? 1.0f / amplitude : 0.0f;
        out[addresses[j]] = raw[j] * std::complex<float>(real * inverse, imag * inverse);

        const float nextReal = real * stepReal - imag * stepImag;
        imag = real * stepImag + imag * stepReal;
        real = nextReal;
    }
}

#endif // DVB_T2_FAST_PILOT_EQUALIZER_H
