#include "Model/FileTree.h"
#include "Model/FolderScanner.h"
#include "TestHelpers.h"

#include <map>

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace rf::test
{
    using namespace rf::model;

    namespace
    {
        juce::StringArray namesOf (const std::vector<ScannedFile>& files)
        {
            juce::StringArray names;

            for (const auto& f : files)
                names.add (f.file.getFileName());

            return names;
        }

        juce::StringArray relativePaths (const std::vector<ScannedFile>& files, const juce::File& root)
        {
            juce::StringArray names;

            for (const auto& f : files)
                names.add (f.file.getRelativePathFrom (root));

            return names;
        }

        const SkippedFile* findSkipped (const ScanResult& r, const juce::String& name)
        {
            for (const auto& s : r.skipped)
                if (s.file.getFileName() == name)
                    return &s;

            return nullptr;
        }
    }

    /*  Fixture layout (created fresh for each test run):

            root/A/a10.wav          mono,   44.1 kHz, 16-bit, 0.5 s
            root/A/a2.wav           stereo, 48 kHz,   24-bit, 0.25 s
            root/A/notes.txt        not audio (ignored inside folders)
            root/A/sub/s1.wav       stereo, 96 kHz,   24-bit
            root/A/sub/deeper/d1.wav mono,  48 kHz,   16-bit
            root/B/b1.wav           mono,   48 kHz,   32-bit float
            root/B/junk.wav         garbage with a .wav extension
            root/B/.hidden.wav      hidden (ignored)
    */
    class FolderScannerTests final : public juce::UnitTest
    {
    public:
        FolderScannerTests() : juce::UnitTest ("FolderScanner", "FolderScanner") {}

        void runTest() override
        {
            TempDirectory tmp;
            const auto root = tmp.get();
            const auto A = tmp.folder ("A");
            const auto B = tmp.folder ("B");

            beginTest ("fixture");
            expect (writeWav (A.getChildFile ("a10.wav"), 1, 44100.0, 16, 22050));
            expect (writeWav (A.getChildFile ("a2.wav"), 2, 48000.0, 24, 12000));
            expect (A.getChildFile ("notes.txt").replaceWithText ("session notes"));
            expect (writeWav (A.getChildFile ("sub/s1.wav"), 2, 96000.0, 24, 9600));
            expect (writeWav (A.getChildFile ("sub/deeper/d1.wav"), 1, 48000.0, 16, 4800));
            expect (writeWav (B.getChildFile ("b1.wav"), 1, 48000.0, 32, 4800));
            expect (writeJunk (B.getChildFile ("junk.wav")));
            expect (writeWav (B.getChildFile (".hidden.wav"), 1, 48000.0, 16, 480));

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            beginTest ("recursion off: only the folder's own audio files, natural order");
            {
                const auto r = FolderScanner::scan ({ A }, false, formats);
                expectEquals (namesOf (r.files).joinIntoString (","), juce::String ("a2.wav,a10.wav"));
                expect (r.skipped.empty(), "notes.txt inside a folder must be ignored, not reported");
                expect (! r.aborted);
            }

            beginTest ("recursion on: files first, then subfolders depth first");
            {
                const auto r = FolderScanner::scan ({ A }, true, formats);
                expectEquals (relativePaths (r.files, root).joinIntoString (","),
                              juce::String ("A/a2.wav,A/a10.wav,A/sub/s1.wav,A/sub/deeper/d1.wav"));
                expect (r.skipped.empty());
            }

            beginTest ("header facts are read");
            {
                const auto r = FolderScanner::scan ({ A.getChildFile ("a2.wav"), B.getChildFile ("b1.wav") }, false, formats);
                expectEquals ((int) r.files.size(), 2);

                const auto& stereo = r.files[0].info;
                expectEquals (stereo.numChannels, 2);
                expectEquals (stereo.sampleRate, 48000.0);
                expectEquals (stereo.bitsPerSample, 24);
                expect (! stereo.isFloatingPoint);
                expectEquals (stereo.lengthInSamples, (juce::int64) 12000);
                expectWithinAbsoluteError (stereo.getDurationSeconds(), 0.25, 1.0e-9);

                const auto& flt = r.files[1].info;
                expectEquals (flt.numChannels, 1);
                expectEquals (flt.bitsPerSample, 32);
                expect (flt.isFloatingPoint);
            }

            beginTest ("dedupe: the same file via folder, file and repeated folder appears once");
            {
                const auto r = FolderScanner::scan ({ A, A.getChildFile ("a2.wav"), A,
                                                      A.getChildFile ("sub/../a2.wav") },
                                                    true, formats);
                expectEquals ((int) r.files.size(), 4);
                expect (r.skipped.empty());
            }

            beginTest ("unreadable file is skipped and reported, hidden files ignored");
            {
                const auto r = FolderScanner::scan ({ B }, false, formats);
                expectEquals (namesOf (r.files).joinIntoString (","), juce::String ("b1.wav"));
                expectEquals ((int) r.skipped.size(), 1);

                const auto* junk = findSkipped (r, "junk.wav");
                expect (junk != nullptr, "junk.wav must be reported");

                if (junk != nullptr)
                    expect (junk->reason.isNotEmpty());
            }

            beginTest ("explicitly given non-audio and missing files are reported");
            {
                const auto r = FolderScanner::scan ({ A.getChildFile ("notes.txt"), root.getChildFile ("missing.wav") },
                                                    false, formats);
                expect (r.files.empty());
                expectEquals ((int) r.skipped.size(), 2);
                expect (findSkipped (r, "notes.txt") != nullptr);
                expect (findSkipped (r, "missing.wav") != nullptr);
            }

           #if JUCE_MAC || JUCE_LINUX
            if (::geteuid() != 0) // root can read anything
            {
                beginTest ("file without read permission is skipped and reported");
                const auto locked = tmp.folder ("C").getChildFile ("locked.wav");
                expect (writeWav (locked, 1, 48000.0, 16, 480));
                expect (::chmod (locked.getFullPathName().toRawUTF8(), 0) == 0);

                const auto r = FolderScanner::scan ({ locked.getParentDirectory() }, false, formats);
                expect (r.files.empty());
                expectEquals ((int) r.skipped.size(), 1);

                ::chmod (locked.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
            }
           #endif

            beginTest ("abort stops the scan");
            {
                const auto r = FolderScanner::scan ({ A, B }, true, formats, [] { return true; });
                expect (r.aborted);
                expect (r.files.empty());
            }

            beginTest ("grouping by folder in the FileTree");
            {
                const auto r = FolderScanner::scan ({ A, B }, true, formats);
                FileTree tree;
                const auto added = tree.add (r.files);

                expectEquals (added.added, 5);
                expectEquals (tree.getNumFiles(), 5);

                const auto& groups = tree.getGroups();
                expectEquals ((int) groups.size(), 4);

                if (groups.size() == 4)
                {
                    expect (groups[0].folder == A);
                    expect (groups[1].folder == A.getChildFile ("sub"));
                    expect (groups[2].folder == A.getChildFile ("sub/deeper"));
                    expect (groups[3].folder == B);

                    expectEquals ((int) groups[0].files.size(), 2);
                    expectEquals ((int) groups[1].files.size(), 1);
                    expectEquals ((int) groups[2].files.size(), 1);
                    expectEquals ((int) groups[3].files.size(), 1);
                }

                beginTest ("FileTree dedupe: adding the same scan again adds nothing");
                const auto again = tree.add (FolderScanner::scan ({ A }, true, formats).files);
                expectEquals (again.added, 0);
                expectEquals (again.duplicates, 4);
                expectEquals (tree.getNumFiles(), 5);
            }

            beginTest ("stable order: adding files in reverse gives the same list");
            {
                auto files = FolderScanner::scan ({ A, B }, true, formats).files;
                FileTree forward, backward;
                forward.add (files);
                std::reverse (files.begin(), files.end());
                backward.add (files);

                // Group order follows first appearance; the files inside each folder must be
                // in the same (natural name) order either way.
                auto folderContents = [] (const FileTree& tree)
                {
                    std::map<juce::String, juce::StringArray> m;

                    for (const auto& g : tree.getGroups())
                        for (const auto& item : g.files)
                            m[g.folder.getFullPathName()].add (item.file.getFileName());

                    return m;
                };

                const auto f = folderContents (forward);
                const auto b = folderContents (backward);
                expect (f == b);
                expectEquals (f.at (A.getFullPathName()).joinIntoString (","), juce::String ("a2.wav,a10.wav"));
            }
        }
    };

    static FolderScannerTests folderScannerTests;
}
