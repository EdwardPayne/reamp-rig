#include "Engine/DeviceSession.h"
#include "Engine/DuplexEngine.h"
#include "FakeAudioDevice.h"

namespace rf::test
{
    using namespace rf::engine;

    namespace
    {
        /** A mono in-memory source: a ramp with a sign pattern, so every sample is distinct. */
        std::shared_ptr<const LoadedSource> makeSource (int numSamples, double rate = 48000.0, float peak = 0.8f)
        {
            auto s = std::make_shared<LoadedSource>();
            s->file = juce::File ("/di/test.wav");
            s->sampleRate = s->fileSampleRate = rate;
            s->fileLengthInSamples = numSamples;
            s->samples.setSize (1, numSamples);

            for (int i = 0; i < numSamples; ++i)
                s->samples.setSample (0, i, peak * ((float) (i % 997) / 996.0f) * (i % 2 == 0 ? 1.0f : -1.0f));

            s->peak = peak;
            return s;
        }

        /** Opens a fake 4-output, 2-input device with the given channels and buffer size. */
        void openDevice (FakeAudioDevice& fake, int bufferSize, int outputChannel, int inputChannel)
        {
            FakeAudioDevice::Type t;
            t.name = "Fake";

            FakeAudioDevice::Device d;
            d.name = "Interface";
            d.inputs = { "In 1", "In 2" };
            d.outputs = { "Out 1", "Out 2", "Out 3", "Out 4" };
            d.bufferSizes = { 64, 256, 480, 1024 };
            t.devices = { d };
            t.defaultInput = t.defaultOutput = d.name;
            fake.types = { t };

            DeviceConfig c;
            c.typeName = "Fake";
            c.inputDevice = c.outputDevice = "Interface";
            c.bufferSize = bufferSize;
            c.outputChannel = outputChannel;
            c.inputChannel = inputChannel;
            fake.open (c);
        }
    }

    /*  Audition rendering through the duplex callback, driven by the fake device: the right
        output channel, the gain, the start position, silence everywhere else, the end of the
        file, stop, and the meters.
    */
    class DuplexEngineTests final : public juce::UnitTest
    {
    public:
        DuplexEngineTests() : juce::UnitTest ("DuplexEngine", "DuplexEngine") {}

        void runTest() override
        {
            for (const auto bufferSize : { 64, 256, 480, 1024 })
            {
                beginTest ("audition plays the source from the start point with gain, buffer " + juce::String (bufferSize));

                FakeAudioDevice fake;
                openDevice (fake, bufferSize, 2, 1);

                DuplexEngine engine;
                engine.setGainDb (-6.0f);
                fake.setCallback (&engine);

                const auto source = makeSource (20000);
                const auto gain = juce::Decibels::decibelsToGain (-6.0f);
                constexpr juce::int64 start = 1234;

                expect (engine.startAudition (source, start));
                const auto out = fake.render (24000);

                auto channelExact = true, othersSilent = true;

                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    const auto srcIndex = start + i;
                    const auto expected = srcIndex < source->getNumSamples() ? source->samples.getSample (0, (int) srcIndex) * gain
                                                                             : 0.0f;
                    channelExact = channelExact && juce::exactlyEqual (out.getSample (2, i), expected);

                    for (int ch : { 0, 1, 3 })
                        othersSilent = othersSilent && juce::exactlyEqual (out.getSample (ch, i), 0.0f);
                }

                expect (channelExact, "output channel 3 must carry source * gain from the start sample, then silence");
                expect (othersSilent, "every other output channel must be zero");

                const auto snap = engine.poll();
                expect (snap.auditioning);
                expect (snap.auditionFinished);
                expectEquals (snap.playheadSample, (juce::int64) source->getNumSamples());
                expectWithinAbsoluteError (snap.outputPeak, 0.8f * gain, 1.0e-6f);
                expect (! snap.outputClipped);

                engine.stopAudition();
                fake.setCallback (nullptr);
            }

            beginTest ("stop mid-way silences the output and releases the source");
            {
                FakeAudioDevice fake;
                openDevice (fake, 256, 0, 0);

                DuplexEngine engine;
                fake.setCallback (&engine);

                auto source = makeSource (48000);
                expect (engine.startAudition (source, 0));

                auto out = fake.render (1024);
                expect (juce::exactlyEqual (out.getSample (0, 1023), source->samples.getSample (0, 1023)));
                expectEquals (engine.poll().playheadSample, (juce::int64) 1024);

                engine.stopAudition();
                out = fake.render (1024);
                expectEquals (out.getMagnitude (0, 0, 1024), 0.0f);

                engine.poll();
                expectEquals ((int) source.use_count(), 1);   // retired command freed after a callback
                expect (! engine.poll().auditioning);

                fake.setCallback (nullptr);
            }

            beginTest ("a gain change ramps within one block, then is exact");
            {
                FakeAudioDevice fake;
                openDevice (fake, 256, 1, 0);

                DuplexEngine engine;
                fake.setCallback (&engine);

                const auto source = makeSource (4096);
                engine.startAudition (source, 0);
                fake.render (256);

                engine.setGainDb (-20.0f);
                const auto out = fake.render (512);
                const auto gain = juce::Decibels::decibelsToGain (-20.0f);

                // Block 2 (ramp): ends exactly at the new gain.
                expectWithinAbsoluteError (out.getSample (1, 255), source->samples.getSample (0, 511) * gain, 1.0e-5f);

                auto exact = true;

                for (int i = 256; i < 512; ++i)
                    exact = exact && juce::exactlyEqual (out.getSample (1, i), source->samples.getSample (0, 256 + i) * gain);

                expect (exact, "after the ramp block the gain is applied exactly");
                engine.stopAudition();
                fake.setCallback (nullptr);
            }

            beginTest ("meters: input peak and latching clip flags, reset by poll");
            {
                FakeAudioDevice fake;
                openDevice (fake, 64, 0, 1);

                DuplexEngine engine;
                fake.setCallback (&engine);

                fake.render (256, [] (juce::int64 i) { return i == 100 ? -0.5f : 0.0f; });
                auto snap = engine.poll();
                expectWithinAbsoluteError (snap.inputPeak, 0.5f, 1.0e-6f);
                expect (! snap.inputClipped);
                expectEquals (snap.outputPeak, 0.0f);

                fake.render (64, [] (juce::int64) { return 1.0f; });
                snap = engine.poll();
                expect (snap.inputClipped);

                snap = engine.poll();
                expect (! snap.inputClipped);
                expectEquals (snap.inputPeak, 0.0f);

                // Output clip: +12 dB on a 0.8 peak.
                engine.setGainDb (12.0f);
                engine.startAudition (makeSource (4096), 0);
                fake.render (4096);
                snap = engine.poll();
                expect (snap.outputClipped);

                engine.stopAudition();
                fake.setCallback (nullptr);
            }

            beginTest ("invalid starts are refused; no output channel means silence");
            {
                FakeAudioDevice fake;
                openDevice (fake, 256, -1, 0);

                DuplexEngine engine;
                fake.setCallback (&engine);

                const auto source = makeSource (1000);
                expect (! engine.startAudition (source, 1000));
                expect (! engine.startAudition (source, -1));
                expect (! engine.startAudition (nullptr, 0));
                expect (engine.startAudition (source, 10));

                const auto out = fake.render (512);

                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    expectEquals (out.getMagnitude (ch, 0, 512), 0.0f);

                engine.stopAudition();
                fake.setCallback (nullptr);
            }
        }
    };

    static DuplexEngineTests duplexEngineTests;
}
