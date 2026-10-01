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

    /** Problems found while recording a file (PROMPT.md 3.3.6, 3.3.7, 3.6.4, 4.4). None of them
        stops the batch; the file is marked and the sidecar log lists them. Bit flags. */
    namespace Warning
    {
        enum : juce::uint32
        {
            resampled     = 1u << 0,    // the device could not run at the file's rate
            notCalibrated = 1u << 1,    // latency estimated from the driver, not measured
            dropout       = 1u << 2,    // xrun, callback gap or record buffer overflow
            silence       = 1u << 3,    // recorded peak below -60 dBFS
            clipped       = 1u << 4,    // recording reached full scale
        };

        /** Warnings that a new take may fix ("Redo files with warnings"). */
        inline constexpr juce::uint32 redoable = dropout | silence | clipped;

        /** Every flag, in display order. */
        inline constexpr juce::uint32 all[] = { notCalibrated, resampled, dropout, silence, clipped };

        /** Short badge code, e.g. "NC", "RS", "XR", "SIL", "CLIP". */
        juce::String code (juce::uint32 flag);

        /** Plain-language description of one flag. */
        juce::String describe (juce::uint32 flag);

        /** Descriptions of every set flag, joined with `separator`. */
        juce::String describeAll (juce::uint32 flags, const juce::String& separator);
    }

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
        juce::File root;                   // the dropped/added folder or file it came from
        Channel channel = Channel::left;   // default for stereo sources
        FileStatus status = FileStatus::queued;
        double progress = 0.0;             // 0..1, driven by the batch
        juce::uint32 warnings = 0;         // Warning flags of the last take
        juce::File outputFile;             // written file of the last take (Done)
        juce::String note;                 // why it was skipped or failed

        /** True for any source with more than one channel (only the first two are selectable). */
        bool hasChannelChoice() const noexcept   { return info.numChannels >= 2; }
    };
}
