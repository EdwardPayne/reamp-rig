#pragma once

#include "Engine/LoopbackTestDevice.h"
#include "Engine/Take.h"
#include "TestHelpers.h"

#include <cmath>

/*  Shared by the loopback end-to-end tests and the sync tests: the LoopbackTestDevice
    configuration, test files, and the Rig that runs a DuplexEngine on the simulated loop.
*/
namespace rf::test
{
    using namespace rf::engine;

    namespace loopback
    {
        inline constexpr int outputChannel = 2;    // "Out 3"
        inline constexpr int inputChannel = 1;     // "In 2" (the other input carries 0.25, so a wrong
                                            // channel can never pass)

        inline LoopbackTestDevice::Options deviceOptions (int delay, float gain = 1.0f, float noise = 0.0f)
        {
            LoopbackTestDevice::Options o;
            o.delay = delay;
            o.gain = gain;
            o.noise = noise;
            return o;
        }

        inline DeviceConfig deviceConfig (double rate, int bufferSize)
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
        inline juce::AudioBuffer<float> readFile (const juce::File& f, double* rate = nullptr, int* bits = nullptr)
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
        inline bool writeTone (const juce::File& file, int numChannels, double rate, int numSamples, int bits = 24)
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

        inline std::shared_ptr<const LoadedSource> load (const juce::File& f, int channel, double deviceRate)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::String error;
            return SourceLoader::load ({ f, channel, deviceRate }, formats, error);
        }
    }

}

namespace rf::test
{
    using namespace loopback;
}
