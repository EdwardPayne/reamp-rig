#include "Engine/LoopbackTestDevice.h"
#include "Engine/Take.h"
#include "TestHelpers.h"

#include <cmath>

namespace rf::test
{
    using namespace rf::engine;

    namespace
    {
        constexpr int outputChannel = 2;    // "Out 3"
        constexpr int inputChannel = 1;     // "In 2" (the other input carries 0.25, so a wrong
                                            // channel can never pass)

        LoopbackTestDevice::Options deviceOptions (int delay, float gain = 1.0f, float noise = 0.0f)
        {
            LoopbackTestDevice::Options o;
            o.delay = delay;
            o.gain = gain;
            o.noise = noise;
            return o;
        }

        DeviceConfig deviceConfig (double rate, int bufferSize)
        {
            DeviceConfig c;
            c.typeName = "Loopback";
            c.inputDevice = c.outputDevice = "Loopback Interface";
            c.sampleRate = rate;
            c.bufferSize = bufferSize;
            c.outputChannel = outputChannel;
            c.inputChannel = inputChannel;
            return c;
        }

        /** Reads a whole mono file as floats. */
        juce::AudioBuffer<float> readFile (const juce::File& f, double* rate = nullptr, int* bits = nullptr)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

            if (reader == nullptr)
                return {};

            juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, true);

            if (rate != nullptr)  *rate = reader->sampleRate;
            if (bits != nullptr)  *bits = (int) reader->bitsPerSample;

            return b;
        }

        /** Writes a mono or stereo test file: per channel a faded mix of two sines (distinct
            per channel), so edges carry no discontinuity (fair for the resampler) and every
            sample is non-trivial. 24-bit unless `bits` says otherwise. */
        bool writeTone (const juce::File& file, int numChannels, double rate, int numSamples, int bits = 24)
        {
            file.getParentDirectory().createDirectory();
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                           .withSampleRate (rate)
                                                           .withNumChannels (numChannels)
                                                           .withBitsPerSample (bits)
                                                           .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                         : juce::AudioFormatWriterOptions::SampleFormat::integral));
            if (writer == nullptr)
                return false;

            juce::AudioBuffer<float> b (numChannels, numSamples);
            const auto fade = (int) (0.01 * rate);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto t = (double) i / rate;
                    auto env = 1.0;

                    if (i < fade)                        env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / fade);
                    else if (i > numSamples - 1 - fade)  env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (numSamples - 1 - i) / fade);

                    const auto f1 = ch == 0 ? 997.0 : 1499.0;
                    const auto v = 0.45 * std::sin (juce::MathConstants<double>::twoPi * f1 * t)
                                 + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 3301.0 * t + ch);
                    b.setSample (ch, i, (float) (env * v));
                }
            }

            // Through JUCE's own reader conversion, like a real file would be.
            return writer->writeFromAudioSampleBuffer (b, 0, numSamples);
        }

        struct Rig
        {
            Rig (LoopbackTestDevice::Options o, double rate, int bufferSize)
                : device (std::move (o))
            {
                device.open (deviceConfig (rate, bufferSize));
                device.setCallback (&engine);
            }

            ~Rig()
            {
                device.setCallback (nullptr);
                engine.poll();
            }

            /** Plays and records one take to completion; returns the result. */
            TakeResult run (TakeSpec spec, juce::String& error)
            {
                Take take;
                error = take.start (std::move (spec), engine, writer);

                if (error.isNotEmpty())
                    return {};

                const auto block = device.getStatus().config.bufferSize * 8;

                for (int guard = 0; take.update (engine.poll()) == Take::Phase::recording && guard < 100000; ++guard)
                    device.render (block);

                const auto deadline = juce::Time::getMillisecondCounter() + 10000;

                while (take.update (engine.poll()) != Take::Phase::done && juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep (1);

                // The engine plays silence after the take; it must not still be taking.
                jassert (! engine.isTaking());
                return take.getResult();
            }

            LoopbackTestDevice device;
            DuplexEngine engine;
            FileWriter writer;
        };

        std::shared_ptr<const LoadedSource> load (const juce::File& f, int channel, double deviceRate)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::String error;
            return SourceLoader::load ({ f, channel, deviceRate }, formats, error);
        }
    }

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
