#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "FileItem.h"

#include <functional>
#include <memory>
#include <vector>

namespace rf::model
{
    /** A readable audio file found by a scan, with its header facts. */
    struct ScannedFile
    {
        juce::File file;
        AudioFileInfo info;
        juce::File root;    // the dropped/added folder (or file) that brought this file in
    };

    /** A file (or folder) the scan could not use, with a plain-language reason. */
    struct SkippedFile
    {
        juce::File file;
        juce::String reason;
    };

    struct ScanResult
    {
        std::vector<ScannedFile> files;     // deduplicated, in list order (see FolderScanner::scan)
        std::vector<SkippedFile> skipped;   // unreadable/unsupported files, reported to the user
        std::vector<juce::File> skippedOutputFolders;  // subfolders named like the output subfolder
        bool aborted = false;
    };

    /*  Turns dropped or chosen files and folders into a list of readable audio files.

        Supported formats are whatever juce::AudioFormatManager::registerBasicFormats() reads
        (WAV, AIFF, FLAC, Ogg Vorbis, plus CoreAudio formats on macOS).

        Order: inputs are handled in the order given. Inside a folder, its files come first
        (natural name order), then each subfolder in natural name order, depth first, when
        recursion is on. Hidden files and symbolic-linked subfolders are ignored while
        recursing. Files in a scanned folder whose extension is not an audio format are
        ignored silently; a file with an audio extension that cannot be opened, and any
        explicitly given file that is not readable audio, is reported in `skipped`.
        The same file (by absolute path) is never returned twice.

        Output subfolders are never sources (phase 6): while recursing, a subfolder whose name
        equals `outputSubfolderName` (the DESTINATION "Subfolder" name, e.g. "Reamped";
        compared case-insensitively where the file system is) is not entered and is listed in
        `skippedOutputFolders` instead. A folder the user adds directly is always scanned.

        scan() is synchronous and thread-agnostic (used by tests). scanAsync() runs it on the
        scanner's own background thread and delivers the result on the message thread.
    */
    class FolderScanner
    {
    public:
        FolderScanner();
        ~FolderScanner();

        /** Synchronous scan. `shouldAbort` is polled between files. An empty
            `outputSubfolderName` skips nothing. */
        static ScanResult scan (const juce::Array<juce::File>& inputs, bool recursive,
                                juce::AudioFormatManager& formats,
                                const std::function<bool()>& shouldAbort = {},
                                const juce::String& outputSubfolderName = {});

        /** Reads the header of one file. Returns false and sets `reason` if it is not usable. */
        static bool readInfo (const juce::File&, juce::AudioFormatManager&, AudioFileInfo& info, juce::String& reason);

        using Callback = std::function<void (ScanResult)>;

        /** Queues a scan on the background thread; `onDone` is called on the message thread
            (never after this scanner has been destroyed). Scans complete in the order queued. */
        void scanAsync (juce::Array<juce::File> inputs, bool recursive, Callback onDone,
                        juce::String outputSubfolderName = {});

        /** Number of queued or running scans whose results have not been delivered yet. */
        int getNumPending() const noexcept   { return pending; }

    private:
        struct Job;

        juce::AudioFormatManager formats;   // used on the scanner thread only
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
        int pending = 0;                    // message thread only

        // Declared last so it is destroyed (and its thread stopped) before the members above.
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Folder scanner")
                                                        .withNumberOfThreads (1) };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FolderScanner)
    };
}
