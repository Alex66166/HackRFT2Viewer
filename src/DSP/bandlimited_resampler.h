#ifndef BANDLIMITED_RESAMPLER_H
#define BANDLIMITED_RESAMPLER_H

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <vector>

/*
 * Streaming band-limited fractional resampler for the DVB-T2 front-end.
 *
 * The original receiver converted every input sample with a cubic Farrow
 * interpolator to about 18.285714 MS/s and then ran a 64-tap FIR/2
 * decimator.  That makes the expensive FIR run at a fixed rate even when
 * HackRF input is reduced from 10 to 8 MS/s.
 *
 * This class performs the conversion directly to the DVB-T2 elementary
 * sample rate.  A bank of fractional-delay FIR phases is precomputed once;
 * each output sample needs one 32-tap real-coefficient complex dot product.
 * The step can be adjusted continuously by the existing timing loop.
 *
 * It intentionally owns a small amount of history/look-ahead so callers can
 * feed arbitrary USB-sized blocks without copying overlap themselves.
 */
class bandlimited_resampler
{
public:
    using complex_type = std::complex<float>;

    static constexpr int Taps = 32;
    static constexpr int Phases = 512;
    static constexpr int Left = Taps / 2 - 1;   // samples before floor(t)
    static constexpr int Right = Taps - Left - 1;

    bandlimited_resampler(double inputRateHz, double outputRateHz)
        : m_inputRateHz(inputRateHz), m_outputRateHz(outputRateHz)
    {
        buildTable();
        reset();
    }

    void reset()
    {
        m_storage.clear();
        m_storage.reserve(1u << 17);
        // Give the first output samples causal zero history.  The right side
        // is provided naturally by look-ahead from the incoming stream.
        m_storage.assign(Left + 2, complex_type{});
        m_storageStart = -static_cast<std::int64_t>(Left + 2);
        m_begin = 0;
        m_totalInput = 0;
        m_nextTime = 0.0;
    }

    double nominalInputStep() const noexcept
    {
        return m_inputRateHz / m_outputRateHz;
    }

    /*
     * inputStep is measured in input samples per output sample.  It is the
     * direct equivalent of 2*(legacy Farrow step), therefore the caller can
     * preserve its existing timing-loop correction exactly.
     *
     * Returns the number of output samples written.  outputCapacity is a hard
     * guard: if it is reached, ungenerated samples remain pending and are
     * emitted on a later call.
     */
    int execute(const int lenIn, const complex_type *input,
                double inputStep, complex_type *output, int outputCapacity)
    {
        if (lenIn <= 0 || input == nullptr || output == nullptr || outputCapacity <= 0)
            return 0;

        if (!std::isfinite(inputStep) || inputStep < 0.25 || inputStep > 8.0)
            inputStep = nominalInputStep();

        compactIfNeeded(static_cast<std::size_t>(lenIn));
        m_storage.insert(m_storage.end(), input, input + lenIn);
        m_totalInput += lenIn;

        int produced = 0;
        const std::int64_t lastAvailable = m_totalInput - 1;

        while (produced < outputCapacity) {
            const std::int64_t center = static_cast<std::int64_t>(m_nextTime);
            const std::int64_t lastNeeded = center + Right;
            if (lastNeeded > lastAvailable)
                break;

            double frac = m_nextTime - static_cast<double>(center);
            int phase = static_cast<int>(frac * Phases);
            if (phase < 0) phase = 0;
            if (phase >= Phases) phase = Phases - 1;

            const std::int64_t firstNeeded = center - Left;
            const std::int64_t relative = firstNeeded - m_storageStart;
            if (relative < 0)
                break; // should only be possible after corrupt state

            const std::size_t pos = m_begin + static_cast<std::size_t>(relative);
            if (pos + Taps > m_storage.size())
                break;

            output[produced++] = dot32(m_storage.data() + pos, phase);
            m_nextTime += inputStep;
        }

        discardOldHistory();
        return produced;
    }

private:
    double m_inputRateHz = 0.0;
    double m_outputRateHz = 0.0;

    // Coefficients are duplicated as h,h for real/imag so the complex dot
    // product maps to straight AVX multiplications over interleaved floats.
    alignas(32) float m_coeff[Phases][Taps * 2]{};

    std::vector<complex_type> m_storage;
    std::size_t m_begin = 0;
    std::int64_t m_storageStart = 0;
    std::int64_t m_totalInput = 0;
    double m_nextTime = 0.0;

    static double sinc(double x) noexcept
    {
        if (std::abs(x) < 1.0e-12)
            return 1.0;
        const double p = 3.141592653589793238462643383279502884 * x;
        return std::sin(p) / p;
    }

    void buildTable()
    {
        // DVB-T2 8 MHz occupies a little less than +/-4 MHz.  Keep the full
        // useful channel while leaving a small transition band.  For higher
        // HackRF rates this also provides the anti-alias filtering needed by
        // the direct down-conversion to 64/7 MS/s.
        constexpr double wantedCutoffHz = 4.15e6;
        const double inputNyquist = 0.5 * m_inputRateHz;
        const double outputNyquist = 0.5 * m_outputRateHz;
        const double cutoffHz = std::min({wantedCutoffHz,
                                          inputNyquist * 0.985,
                                          outputNyquist * 0.94});
        const double cutoff = std::clamp(cutoffHz / inputNyquist, 0.05, 0.985);
        constexpr double pi = 3.141592653589793238462643383279502884;

        for (int p = 0; p < Phases; ++p) {
            const double frac = static_cast<double>(p) / Phases;
            double sum = 0.0;
            double raw[Taps]{};

            for (int j = 0; j < Taps; ++j) {
                const double d = static_cast<double>(j - Left) - frac;
                // Blackman window centred on the fractional-delay kernel.
                const double u = static_cast<double>(j) / (Taps - 1);
                const double window = 0.42 - 0.5 * std::cos(2.0 * pi * u)
                                           + 0.08 * std::cos(4.0 * pi * u);
                raw[j] = cutoff * sinc(cutoff * d) * window;
                sum += raw[j];
            }

            if (std::abs(sum) < 1.0e-15)
                sum = 1.0;
            for (int j = 0; j < Taps; ++j) {
                const float h = static_cast<float>(raw[j] / sum);
                m_coeff[p][j * 2] = h;
                m_coeff[p][j * 2 + 1] = h;
            }
        }
    }

    complex_type dot32(const complex_type *samples, int phase) const noexcept
    {
        const float *x = reinterpret_cast<const float *>(samples);
        const float *h = m_coeff[phase];
        __m256 acc = _mm256_setzero_ps();
        // Taps*2 = 64 floats, eight AVX vectors.
        for (int i = 0; i < Taps * 2; i += 8) {
            const __m256 xv = _mm256_loadu_ps(x + i);
            const __m256 hv = _mm256_loadu_ps(h + i);
            acc = _mm256_add_ps(acc, _mm256_mul_ps(xv, hv));
        }
        alignas(32) float s[8];
        _mm256_store_ps(s, acc);
        return complex_type(s[0] + s[2] + s[4] + s[6],
                            s[1] + s[3] + s[5] + s[7]);
    }

    void discardOldHistory()
    {
        const std::int64_t nextCenter = static_cast<std::int64_t>(m_nextTime);
        const std::int64_t keepFrom = nextCenter - Left - 4;
        if (keepFrom <= m_storageStart)
            return;

        const std::int64_t drop = keepFrom - m_storageStart;
        const std::size_t available = m_storage.size() - m_begin;
        const std::size_t amount = std::min<std::size_t>(static_cast<std::size_t>(drop), available);
        m_begin += amount;
        m_storageStart += static_cast<std::int64_t>(amount);
    }

    void compactIfNeeded(std::size_t incoming)
    {
        if (m_begin == 0)
            return;
        if (m_begin < (1u << 15) && m_storage.size() + incoming < m_storage.capacity())
            return;

        m_storage.erase(m_storage.begin(), m_storage.begin() + static_cast<std::ptrdiff_t>(m_begin));
        m_begin = 0;
    }
};

#endif // BANDLIMITED_RESAMPLER_H
