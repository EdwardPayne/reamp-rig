#include "LoopbackRig.h"

namespace rf::test
{
    using namespace rf::engine;

    /*  End-to-end engine test (PROMPT.md 4.5 and section 7): a file is loaded, played through
        the DuplexEngine into the LoopbackTestDevice (output fed back into the input after one
        buffer plus `delay` samples), recorded through the record FIFO and the writer thread,
        latency-compensated and written to disk. The file that comes back must have exactly
        the source's length and, at 0 dB with a unity loop, be bit-exact with the played
        channel, for mono and stereo sources (L and R), buffers 64/256/480/1024 and several
        delays, including delays shorter and longer than the buffer.
    */
    class LoopbackTests final : public juce::UnitTest
    {
    public:
        LoopbackTests() : juce::UnitTest ("Loopback end to end", "Loopback") {}

        void runTest() override
        {
            TempDirectory dir;

            const auto mono = dir.get().getChildFile ("src/mono.wav");
            const auto stereo = dir.get().getChildFile ("src/stereo.wav");
            constexpr int length = 12345;   // not a multiple of any buffer size

            beginTest ("source files");
            expect (writeTone (mono, 1, 48000.0, length));
            expect (writeTone (stereo, 2, 48000.0, length));

            const auto monoRef = readFile (mono);
            const auto stereoRef = readFile (stereo);

            struct Case { juce::File file; int channel; const juce::AudioBuffer<float>* ref; const char* name; };
            const Case cases[] = { { mono, 0, &monoRef, "mono" }, { stereo, 0, &stereoRef, "stereo L" },
                                   { stereo, 1, &stereoRef, "stereo R" } };

            int index = 0;

            for (const auto bufferSize : { 64, 256, 480, 1024 })
            {
                for (const auto delay : { 0, 1, 37, 256, 1000 })
                {
                    for (const auto& c : cases)
                    {
                        beginTest (juce::String (c.name) + ", buffer " + juce::String (bufferSize) + ", delay "
                                   + juce::String (delay) + ": exact length, bit-exact at 0 dB");

                        Rig rig (deviceOptions (delay), 48000.0, bufferSize);

                        TakeSpec spec;
                        spec.source = load (c.file, c.channel, 48000.0);
                        spec.deviceRate = 48000.0;
                        spec.latencySamples = rig.device.getRoundTripSamples();
                        spec.outputFile = dir.get().getChildFile ("out/take " + juce::String (++index) + ".wav");
                        spec.bitsPerSample = 24;

                        juce::String error;
                        const auto result = rig.run (spec, error);
                        expect (error.isEmpty(), error);
                        expect (result.ok, result.error);
                        expect (! result.hadDropout());
                        expect (! result.resampled);
                        expect (result.notCalibrated);   // a driver estimate, never "measured" here

                        const auto out = readFile (spec.outputFile);
                        expectEquals (out.getNumChannels(), 1);
                        expectEquals (out.getNumSamples(), length);

                        auto exact = out.getNumSamples() == length;

                        for (int i = 0; exact && i < length; ++i)
                            exact = juce::exactlyEqual (out.getSample (0, i), c.ref->getSample (c.channel, i));

                        expect (exact, "recording must equal the played channel sample for sample");
                        expect (! FileWriter::getTempFile (spec.outputFile).exists(), "temp file must be renamed");
                    }
                }
            }

            beginTest ("gain: output level -6 dB and a loop gain of 0.7 match within float tolerance");
            {
                Rig rig (deviceOptions (37, 0.7f), 48000.0, 480);
                rig.engine.setGainDb (-6.0f);

                TakeSpec spec;
                spec.source = load (stereo, 1, 48000.0);
                spec.deviceRate = 48000.0;
                spec.latencySamples = rig.device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile ("out/gain.wav");
                spec.bitsPerSample = 32;

                juce::String error;
                const auto result = rig.run (spec, error);
                expect (result.ok, error + result.error);

                const auto out = readFile (spec.outputFile);
                expectEquals (out.getNumSamples(), length);

                const auto g = juce::Decibels::decibelsToGain (-6.0f) * 0.7f;
                auto worst = 0.0f;

                for (int i = 0; i < juce::jmin (length, out.getNumSamples()); ++i)
                    worst = juce::jmax (worst, std::abs (out.getSample (0, i) - stereoRef.getSample (1, i) * g));

                expectLessThan (worst, 1.0e-6f);
            }

            beginTest ("tail: 25 ms more is recorded, the source part stays exact, the rest is the loop's silence");
            {
                Rig rig (deviceOptions (100), 48000.0, 256);

                TakeSpec spec;
                spec.source = load (mono, 0, 48000.0);
                spec.deviceRate = 48000.0;
                spec.latencySamples = rig.device.getRoundTripSamples();
                spec.tailSamples = 1200;
                spec.outputFile = dir.get().getChildFile ("out/tail.wav");

                juce::String error;
                const auto result = rig.run (spec, error);
                expect (result.ok, error + result.error);

                const auto out = readFile (spec.outputFile);
                expectEquals (out.getNumSamples(), length + 1200);

                auto exact = out.getNumSamples() == length + 1200;

                for (int i = 0; exact && i < length; ++i)
                    exact = juce::exactlyEqual (out.getSample (0, i), monoRef.getSample (0, i));

                for (int i = length; exact && i < length + 1200; ++i)
                    exact = juce::exactlyEqual (out.getSample (0, i), 0.0f);

                expect (exact);
            }

            beginTest ("delay much larger than the buffer (buffer 64, delay 3000)");
            {
                Rig rig (deviceOptions (3000), 48000.0, 64);

                TakeSpec spec;
                spec.source = load (stereo, 0, 48000.0);
                spec.deviceRate = 48000.0;
                spec.latencySamples = rig.device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile ("out/long delay.wav");

                juce::String error;
                const auto result = rig.run (spec, error);
                expect (result.ok, error + result.error);

                const auto out = readFile (spec.outputFile);
                auto exact = out.getNumSamples() == length;

                for (int i = 0; exact && i < length; ++i)
                    exact = juce::exactlyEqual (out.getSample (0, i), stereoRef.getSample (0, i));

                expect (exact);
            }

            beginTest ("16-bit and 32-bit float sources come back exact in their own format");
            {
                for (const auto bits : { 16, 32 })
                {
                    const auto src = dir.get().getChildFile ("src/" + juce::String (bits) + ".wav");
                    expect (writeTone (src, 2, 48000.0, 9000, bits));
                    const auto ref = readFile (src);

                    Rig rig (deviceOptions (37), 48000.0, 256);

                    TakeSpec spec;
                    spec.source = load (src, 1, 48000.0);
                    spec.deviceRate = 48000.0;
                    spec.latencySamples = rig.device.getRoundTripSamples();
                    spec.outputFile = dir.get().getChildFile ("out/" + juce::String (bits) + ".wav");
                    spec.bitsPerSample = bits;

                    juce::String error;
                    const auto result = rig.run (spec, error);
                    expect (result.ok, error + result.error);

                    int readBits = 0;
                    const auto out = readFile (spec.outputFile, nullptr, &readBits);
                    expectEquals (readBits, bits);

                    auto exact = out.getNumSamples() == 9000;

                    for (int i = 0; exact && i < 9000; ++i)
                        exact = juce::exactlyEqual (out.getSample (0, i), ref.getSample (1, i));

                    expect (exact, juce::String (bits) + "-bit");
                }
            }

            beginTest ("resampled: 44.1 kHz source on a device fixed at 48 kHz");
            {
                const auto src = dir.get().getChildFile ("src/44k.wav");
                constexpr int srcLength = 22050;
                expect (writeTone (src, 2, 44100.0, srcLength));
                const auto ref = readFile (src);

                auto options = deviceOptions (37);
                options.sampleRates = { 48000.0 };
                Rig rig (options, 44100.0, 256);
                expectEquals (rig.device.getStatus().config.sampleRate, 48000.0);

                TakeSpec spec;
                spec.source = load (src, 1, 48000.0);
                expect (spec.source != nullptr && spec.source->resampled);
                spec.deviceRate = 48000.0;
                spec.latencySamples = rig.device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile ("out/resampled.wav");

                juce::String error;
                const auto result = rig.run (spec, error);
                expect (result.ok, error + result.error);
                expect (result.resampled);
                expect (! result.hadDropout());

                double rate = 0.0;
                const auto out = readFile (spec.outputFile, &rate);
                expectEquals (rate, 44100.0);
                expectEquals (out.getNumSamples(), srcLength);

                auto worst = 0.0f;

                for (int i = 0; i < juce::jmin (srcLength, out.getNumSamples()); ++i)
                    worst = juce::jmax (worst, std::abs (out.getSample (0, i) - ref.getSample (1, i)));

                const auto db = juce::Decibels::gainToDecibels (worst, -400.0f);
                logMessage ("    resampled round trip through the loop: max error " + juce::String (db, 1) + " dBFS");
                expectLessThan (db, -80.0f);
            }
        }
    };

    static LoopbackTests loopbackTests;
}
