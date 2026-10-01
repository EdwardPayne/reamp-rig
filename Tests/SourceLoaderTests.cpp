#include "Engine/SourceLoader.h"
#include "TestHelpers.h"

namespace rf::test
{
    using namespace rf::engine;

    /*  Decoding the played channel into memory for audition, its peak, resampling to the
        device rate (aligned, no latency), and failures.
    */
    class SourceLoaderTests final : public juce::UnitTest
    {
    public:
        SourceLoaderTests() : juce::UnitTest ("SourceLoader", "SourceLoader") {}

        void runTest() override
        {
            TempDirectory dir;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            beginTest ("test file");
            const auto stereo = dir.get().getChildFile ("stereo.wav");
            expect (writeWav (stereo, 2, 48000.0, 24, 30000));

            juce::AudioBuffer<float> reference (2, 30000);
            {
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (stereo));
                reader->read (&reference, 0, 30000, 0, true, true);
            }

            beginTest ("decodes the chosen channel only, with its peak");
            {
                for (int channel : { 0, 1 })
                {
                    juce::String error;
                    const auto s = SourceLoader::load ({ stereo, channel, 0.0 }, formats, error);

                    expect (s != nullptr, error);
                    expectEquals (s->channel, channel);
                    expectEquals (s->getNumSamples(), 30000);
                    expectEquals (s->sampleRate, 48000.0);
                    expect (! s->resampled);

                    auto same = true;

                    for (int i = 0; i < 30000; ++i)
                        same = same && juce::exactlyEqual (s->samples.getSample (0, i), reference.getSample (channel, i));

                    expect (same);
                    expectEquals (s->peak, reference.getMagnitude (channel, 0, 30000));
                }
            }

            beginTest ("same rate as the device: no resampling");
            {
                juce::String error;
                const auto s = SourceLoader::load ({ stereo, 1, 48000.0 }, formats, error);
                expect (s != nullptr && ! s->resampled && s->getNumSamples() == 30000);
            }

            beginTest ("resamples to the device rate, time-aligned");
            {
                juce::String error;
                const auto s = SourceLoader::load ({ stereo, 0, 44100.0 }, formats, error);

                expect (s != nullptr, error);
                expect (s->resampled);
                expectEquals (s->sampleRate, 44100.0);
                expectEquals (s->fileSampleRate, 48000.0);
                expectEquals (s->getNumSamples(), (int) std::ceil (30000.0 * 44100.0 / 48000.0));
                expectEquals (s->peak, reference.getMagnitude (0, 0, 30000));   // peak of the file itself

                // Compare against the original, linearly interpolated at the same instants
                // (a 110 Hz sine is smooth enough for that), away from both ends. `shift` moves
                // the comparison by a fraction of a sample to prove the alignment.
                auto maxError = [&] (double shift)
                {
                    auto e = 0.0f;

                    for (int i = 200; i < s->getNumSamples() - 200; ++i)
                    {
                        const auto t = (double) i * 48000.0 / 44100.0 + shift;
                        const auto k = (int) t;
                        const auto frac = (float) (t - k);
                        const auto expected = reference.getSample (0, k) * (1.0f - frac) + reference.getSample (0, k + 1) * frac;
                        e = juce::jmax (e, std::abs (s->samples.getSample (0, i) - expected));
                    }

                    return e;
                };

                // JUCE's windowed sinc leaves about -40 dB of error on this signal (fine for a
                // level-setting preview); a misalignment of half a sample doubles it.
                const auto aligned = maxError (0.0);
                expectLessThan (aligned, 0.0075f);
                expectGreaterThan (maxError (-0.5), aligned);
                expectGreaterThan (maxError (0.5), aligned);
            }

            beginTest ("a channel beyond the file's channels is clamped");
            {
                const auto mono = dir.get().getChildFile ("mono.wav");
                expect (writeWav (mono, 1, 44100.0, 16, 1000));

                juce::String error;
                const auto s = SourceLoader::load ({ mono, 1, 0.0 }, formats, error);
                expect (s != nullptr);
                expectEquals (s->channel, 0);
            }

            beginTest ("unreadable file and abort");
            {
                const auto junk = dir.get().getChildFile ("junk.wav");
                expect (writeJunk (junk));

                juce::String error;
                expect (SourceLoader::load ({ junk, 0, 0.0 }, formats, error) == nullptr);
                expectEquals (error, juce::String ("not a readable audio file"));

                error = {};
                expect (SourceLoader::load ({ dir.get().getChildFile ("missing.wav"), 0, 0.0 }, formats, error) == nullptr);
                expect (error.isNotEmpty());

                error = {};
                expect (SourceLoader::load ({ stereo, 0, 0.0 }, formats, error, [] { return true; }) == nullptr);
                expectEquals (error, juce::String ("cancelled"));
            }
        }
    };

    static SourceLoaderTests sourceLoaderTests;
}
