#include "FolderScanner.h"

#include <set>

namespace rf::model
{
    namespace
    {
        juce::String pathKey (const juce::File& f)
        {
            const auto path = f.getFullPathName();
            return juce::File::areFileNamesCaseSensitive() ? path : path.toLowerCase();
        }

        bool naturalLess (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        }

        bool hasAudioExtension (const juce::File& f, juce::AudioFormatManager& formats)
        {
            return f.getFileExtension().isNotEmpty()
                && formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
        }

        struct Scan
        {
            bool recursive;
            juce::AudioFormatManager& formats;
            const std::function<bool()>& shouldAbort;

            ScanResult result;
            std::set<juce::String> seen;
            juce::File root;    // the input currently being scanned

            bool aborted()
            {
                if (! result.aborted && shouldAbort && shouldAbort())
                    result.aborted = true;

                return result.aborted;
            }

            void addFile (const juce::File& file, bool explicitlyGiven)
            {
                if (! seen.insert (pathKey (file)).second)
                    return;

                if (! explicitlyGiven && ! hasAudioExtension (file, formats))
                    return; // stray non-audio file inside a folder: ignored, not reported

                AudioFileInfo info;
                juce::String reason;

                if (FolderScanner::readInfo (file, formats, info, reason))
                    result.files.push_back ({ file, info, root });
                else
                    result.skipped.push_back ({ file, reason });
            }

            void addFolder (const juce::File& folder)
            {
                if (! folder.hasReadAccess())
                {
                    result.skipped.push_back ({ folder, "folder is not readable" });
                    return;
                }

                std::vector<juce::File> files, folders;

                for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*",
                                                                        juce::File::findFilesAndDirectories
                                                                            | juce::File::ignoreHiddenFiles))
                {
                    if (entry.isDirectory())
                    {
                        if (! entry.getFile().isSymbolicLink())
                            folders.push_back (entry.getFile());
                    }
                    else
                    {
                        files.push_back (entry.getFile());
                    }
                }

                std::sort (files.begin(), files.end(), naturalLess);
                std::sort (folders.begin(), folders.end(), naturalLess);

                for (const auto& f : files)
                {
                    if (aborted())
                        return;

                    addFile (f, false);
                }

                if (recursive)
                    for (const auto& sub : folders)
                        if (! aborted())
                            addFolder (sub);
            }
        };
    }

    //==============================================================================
    bool FolderScanner::readInfo (const juce::File& file, juce::AudioFormatManager& formats,
                                  AudioFileInfo& info, juce::String& reason)
    {
        if (! file.existsAsFile())
        {
            reason = "file not found";
            return false;
        }

        if (! file.hasReadAccess())
        {
            reason = "no permission to read the file";
            return false;
        }

        if (! hasAudioExtension (file, formats))
        {
            reason = "not a supported audio format";
            return false;
        }

        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            reason = "not a readable audio file";
            return false;
        }

        if (reader->numChannels == 0 || reader->sampleRate <= 0.0)
        {
            reason = "invalid audio header";
            return false;
        }

        if (reader->lengthInSamples <= 0)
        {
            reason = "contains no audio";
            return false;
        }

        info.numChannels     = (int) reader->numChannels;
        info.sampleRate      = reader->sampleRate;
        info.bitsPerSample   = (int) reader->bitsPerSample;
        info.isFloatingPoint = reader->usesFloatingPointData;
        info.lengthInSamples = reader->lengthInSamples;
        info.formatName      = reader->getFormatName();
        return true;
    }

    ScanResult FolderScanner::scan (const juce::Array<juce::File>& inputs, bool recursive,
                                    juce::AudioFormatManager& formats,
                                    const std::function<bool()>& shouldAbort)
    {
        Scan scan { recursive, formats, shouldAbort, {}, {}, {} };

        for (const auto& input : inputs)
        {
            if (scan.aborted())
                break;

            scan.root = input;

            if (input.isDirectory())
                scan.addFolder (input);
            else if (input.existsAsFile())
                scan.addFile (input, true);
            else
                scan.result.skipped.push_back ({ input, "file not found" });
        }

        return std::move (scan.result);
    }

    //==============================================================================
    struct FolderScanner::Job final : public juce::ThreadPoolJob
    {
        Job (FolderScanner& s, juce::Array<juce::File> in, bool rec, Callback cb)
            : ThreadPoolJob ("Folder scan"),
              scanner (s), inputs (std::move (in)), recursive (rec), onDone (std::move (cb)),
              alive (s.alive)
        {
        }

        JobStatus runJob() override
        {
            auto result = FolderScanner::scan (inputs, recursive, scanner.formats, [this] { return shouldExit(); });

            juce::MessageManager::callAsync ([flag = alive, &owner = scanner,
                                              callback = std::move (onDone),
                                              r = std::move (result)]() mutable
            {
                if (! flag->load())
                    return;

                --owner.pending;

                if (callback != nullptr)
                    callback (std::move (r));
            });

            return jobHasFinished;
        }

        FolderScanner& scanner;
        juce::Array<juce::File> inputs;
        bool recursive;
        Callback onDone;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    FolderScanner::FolderScanner()
    {
        formats.registerBasicFormats();
    }

    FolderScanner::~FolderScanner()
    {
        alive->store (false);
        pool.removeAllJobs (true, 5000);
    }

    void FolderScanner::scanAsync (juce::Array<juce::File> inputs, bool recursive, Callback onDone)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        ++pending;
        pool.addJob (new Job (*this, std::move (inputs), recursive, std::move (onDone)), true);
    }
}
