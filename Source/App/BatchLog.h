#pragma once

#include <juce_core/juce_core.h>

namespace rf::app
{
    /*  The sidecar log of one batch (PROMPT.md 3.4.7): a plain-text file written next to the
        results, "Reamp Rig batch <date> <time>.txt". One log per batch, placed in the
        destination folder of the first file the batch handles (the output folder itself in
        single-folder mode, the first file's subfolder in subfolder mode). The header is
        written when the first file is known, then one entry is appended per file as it
        finishes, so an interrupted batch still leaves a complete record of what was done.

        Message thread only (small appends between takes, never on the audio thread).
    */
    class BatchLog
    {
    public:
        /** Creates the file in `folder` with the header lines. Returns false (and logs to
            stderr) if it cannot be written; the batch continues without a log. */
        bool open (const juce::File& folder, const juce::StringArray& headerLines);

        void append (const juce::StringArray& lines);
        void close (const juce::StringArray& footerLines);

        bool isOpen() const noexcept                     { return file != juce::File(); }
        const juce::File& getFile() const noexcept       { return file; }

    private:
        juce::File file;
    };
}
