#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace rf::test
{
    /** A fresh, empty directory under the system temp folder, deleted with everything in it
        when this object goes out of scope. */
    class TempDirectory
    {
    public:
        TempDirectory()
            : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getNonexistentChildFile ("reamp-rig-test", {}, false))
        {
            root.createDirectory();
        }

        ~TempDirectory()   { root.deleteRecursively(); }

        const juce::File& get() const noexcept   { return root; }

        juce::File folder (const juce::String& relativePath) const
        {
            auto f = root.getChildFile (relativePath);
            f.createDirectory();
            return f;
        }

    private:
        juce::File root;

        JUCE_DECLARE_NON_COPYABLE (TempDirectory)
    };

    /** Writes a short WAV file with a decaying sine burst. Returns false on failure. */
    inline bool writeWav (const juce::File& file, int numChannels, double sampleRate, int bitsPerSample,
                          int numSamples)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (numChannels)
                                                       .withBitsPerSample (bitsPerSample));
        if (writer == nullptr)
            return false;

        juce::AudioBuffer<float> buffer (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, 0.5f * std::exp (-3.0f * (float) i / (float) numSamples)
                                             * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (float) (ch + 1)
                                                         * (float) i / (float) sampleRate));

        return writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    }

    /** Writes bytes that no audio format can parse. */
    inline bool writeJunk (const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        return file.replaceWithText ("This is not audio. RIFF? No. Just text pretending to be a WAV file.");
    }
}
