#include "Engine/Resampler.h"

#include <cmath>

namespace rf::test
{
    using namespace rf::engine;

    namespace
    {
        constexpr double sineHz = 1000.0;
        constexpr double sineLevel = 0.5;           // -6 dBFS
        constexpr double fadeSeconds = 0.01;        // raised-cosine fades: no edge clicks

        /** The test signal as a continuous function of time: a faded 1 kHz sine. */
        double signalAt (double t, double duration)
        {
            if (t < 0.0 || t > duration)
                return 0.0;

            auto env = 1.0;

            if (t < fadeSeconds)
                env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t / fadeSeconds);
            else if (t > duration - fadeSeconds)
                env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (duration - t) / fadeSeconds);

            return sineLevel * env * std::sin (juce::MathConstants<double>::twoPi * sineHz * t);
        }

        std::vector<float> render (double rate, juce::int64 length, double duration)
        {
            std::vector<float> v ((size_t) length);

            for (juce::int64 i = 0; i < length; ++i)
                v[(size_t) i] = (float) signalAt ((double) i / rate, duration);

            return v;
        }

        /** Largest deviation from the ideal signal, in dB relative to full scale. */
        double errorDb (const std::vector<float>& actual, const std::vector<float>& ideal)
        {
            auto worst = 0.0;

            for (size_t i = 0; i < actual.size() && i < ideal.size(); ++i)
                worst = juce::jmax (worst, std::abs ((double) actual[i] - (double) ideal[i]));

            return worst > 0.0 ? 20.0 * std::log10 (worst) : -400.0;
        }
    }

    /*  The record path's resampler (PROMPT.md 4.4): exact lengths, sample-0 alignment, and the
        error on a test sine, which ARCHITECTURE.md documents. The threshold for the record
        path is -80 dB (JUCE's interpolator in the phase 3 preview reached only about -40 dB).
    */
    class ResamplerTests final : public juce::UnitTest
    {
    public:
        ResamplerTests() : juce::UnitTest ("Resampler", "Resampler") {}

        /** Documented error threshold for one conversion and for a round trip. */
        static constexpr double thresholdDb = -80.0;

        void runTest() override
        {
            beginTest ("output lengths");
            {
                expectEquals (Resampler::getOutputLength (44100, 44100.0, 48000.0), (juce::int64) 48000);
                expectEquals (Resampler::getOutputLength (44101, 44100.0, 48000.0), (juce::int64) 48002);   // ceil
                expectEquals (Resampler::getOutputLength (48000, 48000.0, 44100.0), (juce::int64) 44100);
                expectEquals (Resampler::getOutputLength (1, 96000.0, 44100.0), (juce::int64) 1);
                expectEquals (Resampler::getOutputLength (0, 44100.0, 48000.0), (juce::int64) 0);
            }

            beginTest ("exact positions over a long file (no drift)");
            {
                const Resampler r (44100.0, 48000.0);
                juce::int64 integer;
                double fraction;

                r.getInputPosition (160 * 1000000LL, integer, fraction);   // 147/160 exactly
                expectEquals (integer, 147 * 1000000LL);
                expectEquals (fraction, 0.0);

                r.getInputPosition (1, integer, fraction);
                expectEquals (integer, (juce::int64) 0);
                expectWithinAbsoluteError (fraction, 147.0 / 160.0, 1.0e-15);
            }

            const auto duration = 0.5;

            for (const auto& [from, to] : { std::pair (44100.0, 48000.0), std::pair (48000.0, 44100.0),
                                            std::pair (44100.0, 96000.0), std::pair (96000.0, 48000.0) })
            {
                beginTest ("1 kHz sine " + juce::String (from) + " -> " + juce::String (to) + " Hz");

                const auto inLength = (juce::int64) std::llround (duration * from);
                const auto input = render (from, inLength, duration);
                const auto outLength = Resampler::getOutputLength (inLength, from, to);
                std::vector<float> output ((size_t) outLength);

                expect (Resampler (from, to).process (input.data(), inLength, output.data(), 0, outLength));

                const auto e = errorDb (output, render (to, outLength, duration));
                logMessage ("    error " + juce::String (e, 1) + " dBFS");
                expectLessThan (e, thresholdDb);
            }

            beginTest ("round trip 44.1 -> 48 -> 44.1 kHz comes back aligned");
            {
                const auto inLength = (juce::int64) std::llround (duration * 44100.0);
                const auto input = render (44100.0, inLength, duration);
                const auto midLength = Resampler::getOutputLength (inLength, 44100.0, 48000.0);
                std::vector<float> mid ((size_t) midLength), back ((size_t) inLength);

                Resampler (44100.0, 48000.0).process (input.data(), inLength, mid.data(), 0, midLength);
                Resampler (48000.0, 44100.0).process (mid.data(), midLength, back.data(), 0, inLength);

                const auto e = errorDb (back, input);
                logMessage ("    round-trip error " + juce::String (e, 1) + " dBFS");
                expectLessThan (e, thresholdDb);

                // Shifted by one sample the error is about -23 dB: the alignment is real.
                std::vector<float> shifted (back.begin() + 1, back.end());
                expectGreaterThan (errorDb (shifted, input), -40.0);
            }

            beginTest ("streaming gives the same samples as offline, for any block sizes");
            {
                const auto inLength = (juce::int64) 20000;
                const auto input = render (48000.0, inLength, (double) inLength / 48000.0);
                const auto outLength = Resampler::getOutputLength (inLength, 48000.0, 44100.0);

                std::vector<float> offline ((size_t) outLength);
                Resampler (48000.0, 44100.0).process (input.data(), inLength, offline.data(), 0, outLength);

                ResamplerStream stream (48000.0, 44100.0);
                std::vector<float> streamed;
                std::vector<float> chunk (777);
                juce::Random random (3);

                for (juce::int64 pos = 0; pos < inLength;)
                {
                    const auto n = (int) juce::jmin ((juce::int64) random.nextInt ({ 1, 3000 }), inLength - pos);
                    stream.push (input.data() + pos, n);
                    pos += n;

                    for (int got; (got = stream.pull (chunk.data(), (int) chunk.size())) > 0;)
                        streamed.insert (streamed.end(), chunk.begin(), chunk.begin() + got);
                }

                stream.finish();

                while ((juce::int64) streamed.size() < outLength)
                {
                    const auto got = stream.pull (chunk.data(), (int) juce::jmin ((juce::int64) chunk.size(),
                                                                                  outLength - (juce::int64) streamed.size()));
                    streamed.insert (streamed.end(), chunk.begin(), chunk.begin() + got);
                }

                expectEquals ((juce::int64) streamed.size(), outLength);

                auto same = true;

                for (size_t i = 0; i < offline.size(); ++i)
                    same = same && juce::exactlyEqual (offline[i], streamed[i]);

                expect (same, "streamed output must equal the offline output sample for sample");
            }
        }
    };

    static ResamplerTests resamplerTests;
}
