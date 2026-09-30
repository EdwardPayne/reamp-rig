#pragma once

#include <juce_core/juce_core.h>

namespace rf::model
{
    /** Unique, stable identifier of a file row for the lifetime of the app. 0 means "none". */
    using ItemId = juce::uint32;

    /** Processing state of one file (PROMPT.md section 3.1.5). */
    enum class FileStatus { queued, recording, done, skipped, error };

    /** Which channel of a stereo source is played to the amp. Mono sources ignore it. */
    enum class Channel { left, right };

    juce::String toString (FileStatus);

    /** Format facts read from the file header by the FolderScanner. */
    struct AudioFileInfo
    {
        int numChannels = 0;
        double sampleRate = 0.0;
        int bitsPerSample = 0;
        bool isFloatingPoint = false;
        juce::int64 lengthInSamples = 0;
        juce::String formatName;

        double getDurationSeconds() const noexcept
        {
            return sampleRate > 0.0 ? (double) lengthInSamples / sampleRate : 0.0;
        }
    };

    /** One source file in the list. */
    struct FileItem
    {
        ItemId id = 0;
        juce::File file;
        AudioFileInfo info;
        Channel channel = Channel::left;   // default for stereo sources
        FileStatus status = FileStatus::queued;
        double progress = 0.0;             // 0..1, driven by the batch in phase 4

        /** True for any source with more than one channel (only the first two are selectable). */
        bool hasChannelChoice() const noexcept   { return info.numChannels >= 2; }
    };
}
