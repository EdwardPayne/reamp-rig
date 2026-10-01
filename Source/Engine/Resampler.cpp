#include "Resampler.h"

#include <cmath>
#include <numeric>

namespace rf::engine
{
    namespace
    {
        constexpr int zeroCrossings = 64;           // per side
        constexpr int tableResolution = 2048;       // table entries per zero crossing
        constexpr double kaiserBeta = 10.0;         // about 100 dB stopband
        constexpr double cutoffShare = 0.94;        // cutoff = 0.47 * the lower rate

        double besselI0 (double x)
        {
            // Power series; converges quickly for the arguments used here (|x| <= beta).
            auto sum = 1.0, term = 1.0;
            const auto halfX = x * 0.5;

            for (int k = 1; k < 64; ++k)
            {
                term *= (halfX / k) * (halfX / k);
                sum += term;

                if (term < sum * 1.0e-17)
                    break;
            }

            return sum;
        }

        bool isIntegerRate (double r)
        {
            return r > 0.0 && r < 1.0e9 && juce::exactlyEqual (r, std::round (r));
        }
    }

    /** sinc(u) * kaiser(u / zeroCrossings) for u in [0, zeroCrossings], tabulated. */
    struct Resampler::Kernel
    {
        Kernel()
        {
            const auto size = zeroCrossings * tableResolution + 2;
            table.resize ((size_t) size);
            const auto norm = 1.0 / besselI0 (kaiserBeta);

            for (int i = 0; i < size; ++i)
            {
                const auto u = (double) i / tableResolution;

                if (u >= zeroCrossings)
                {
                    table[(size_t) i] = 0.0f;
                    continue;
                }

                const auto x = u / zeroCrossings;
                const auto window = besselI0 (kaiserBeta * std::sqrt (1.0 - x * x)) * norm;
                const auto sinc = i == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * u) / (juce::MathConstants<double>::pi * u);
                table[(size_t) i] = (float) (sinc * window);
            }
        }

        std::vector<float> table;
    };

    Resampler::Resampler (double inputRate, double outputRate)
        : inRate (inputRate), outRate (outputRate)
    {
        jassert (inputRate > 0.0 && outputRate > 0.0);

        ratio = inRate / outRate;
        scale = cutoffShare * juce::jmin (1.0, outRate / inRate);
        halfWidth = (int) std::ceil (zeroCrossings / scale) + 1;

        if (isIntegerRate (inRate) && isIntegerRate (outRate))
        {
            const auto in = (juce::int64) inRate, out = (juce::int64) outRate;
            const auto g = std::gcd (in, out);
            inStep = in / g;
            outStep = out / g;
        }

        static const std::shared_ptr<const Kernel> shared = std::make_shared<Kernel>();
        kernel = shared;
    }

    juce::int64 Resampler::getOutputLength (juce::int64 inputLength, double inputRate, double outputRate)
    {
        if (isIntegerRate (inputRate) && isIntegerRate (outputRate))
        {
            const auto in = (juce::int64) inputRate, out = (juce::int64) outputRate;
            const auto g = std::gcd (in, out);
            const auto num = inputLength * (out / g);
            const auto den = in / g;
            return (num + den - 1) / den;
        }

        return (juce::int64) std::ceil ((double) inputLength * outputRate / inputRate - 1.0e-9);
    }

    void Resampler::getInputPosition (juce::int64 m, juce::int64& integer, double& fraction) const noexcept
    {
        if (outStep > 0)
        {
            const auto num = m * inStep;
            integer = num / outStep;
            fraction = (double) (num % outStep) / (double) outStep;
            return;
        }

        const auto p = (double) m * ratio;
        integer = (juce::int64) std::floor (p);
        fraction = p - (double) integer;
    }

    float Resampler::evaluate (const float* in, juce::int64 inStart, juce::int64 inEnd,
                               juce::int64 integer, double fraction) const noexcept
    {
        const auto* table = kernel->table.data();
        const auto first = juce::jmax (inStart, integer - halfWidth);
        const auto last  = juce::jmin (inEnd - 1, integer + halfWidth);

        auto sum = 0.0;

        for (auto n = first; n <= last; ++n)
        {
            // Distance from the output position to input sample n, in zero crossings.
            const auto u = std::abs ((double) (integer - n) + fraction) * scale * tableResolution;
            const auto index = (int) u;

            if (index >= zeroCrossings * tableResolution)
                continue;

            const auto frac = (float) (u - index);
            const auto weight = table[index] + (table[index + 1] - table[index]) * frac;
            sum += (double) in[n - inStart] * (double) weight;
        }

        return (float) (sum * scale);
    }

    bool Resampler::process (const float* in, juce::int64 inLength, float* out, juce::int64 firstOutput,
                             juce::int64 numOutput, const std::function<bool()>& shouldAbort) const
    {
        for (juce::int64 i = 0; i < numOutput; ++i)
        {
            if ((i & 4095) == 0 && shouldAbort != nullptr && shouldAbort())
                return false;

            juce::int64 integer;
            double fraction;
            getInputPosition (firstOutput + i, integer, fraction);
            out[i] = evaluate (in, 0, inLength, integer, fraction);
        }

        return true;
    }

    //==============================================================================
    ResamplerStream::ResamplerStream (double inputRate, double outputRate)
        : resampler (inputRate, outputRate)
    {
    }

    void ResamplerStream::push (const float* data, int numSamples)
    {
        jassert (! finished);
        buffer.insert (buffer.end(), data, data + numSamples);
        numInput += numSamples;
    }

    int ResamplerStream::pull (float* dest, int maxSamples)
    {
        int produced = 0;

        while (produced < maxSamples)
        {
            juce::int64 integer;
            double fraction;
            resampler.getInputPosition (nextOutput, integer, fraction);

            if (! finished && integer + resampler.getHalfWidth() >= numInput)
                break;   // needs input that has not arrived yet

            dest[produced++] = resampler.evaluate (buffer.data(), bufferStart, numInput, integer, fraction);
            ++nextOutput;
        }

        // Drop input that no future output needs (in large steps, to keep erasing cheap).
        juce::int64 integer;
        double fraction;
        resampler.getInputPosition (nextOutput, integer, fraction);
        const auto keepFrom = integer - resampler.getHalfWidth() - 1;

        if (keepFrom - bufferStart > 1 << 16)
        {
            const auto drop = juce::jmin ((juce::int64) buffer.size(), keepFrom - bufferStart);
            buffer.erase (buffer.begin(), buffer.begin() + (std::ptrdiff_t) drop);
            bufferStart += drop;
        }

        return produced;
    }
}
