#include "Model/FileTree.h"
#include "Model/FolderScanner.h"
#include "Model/OutputNaming.h"
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

            beginTest ("output subfolders are never scanned as sources");
            {
                // A/Reamped/ holds earlier results, A/sub/Reamped/ too (nested), and
                // A/sub/reamped2/ only starts with the name. Recursion on.
                expect (writeWav (A.getChildFile ("Reamped/a2_reamp.wav"), 1, 48000.0, 24, 480));
                expect (writeWav (A.getChildFile ("sub/Reamped/s1_reamp.wav"), 1, 96000.0, 24, 480));
                expect (writeWav (A.getChildFile ("sub/Reamped2/keep.wav"), 1, 48000.0, 24, 480));

                const auto r = FolderScanner::scan ({ A }, true, formats, {}, "Reamped");
                expectEquals (relativePaths (r.files, root).joinIntoString (","),
                              juce::String ("A/a2.wav,A/a10.wav,A/sub/s1.wav,A/sub/deeper/d1.wav,A/sub/Reamped2/keep.wav"));
                expectEquals ((int) r.skippedOutputFolders.size(), 2);

                if (r.skippedOutputFolders.size() == 2)
                {
                    expect (r.skippedOutputFolders[0] == A.getChildFile ("Reamped"));
                    expect (r.skippedOutputFolders[1] == A.getChildFile ("sub/Reamped"));
                }

                expect (r.skipped.empty(), "an output folder is not an unreadable file");

                beginTest ("output subfolder: another name, case, no name, recursion off, added directly");

                // Another configured name skips that one instead.
                const auto other = FolderScanner::scan ({ A }, true, formats, {}, "deeper");
                expect (! relativePaths (other.files, root).contains ("A/sub/deeper/d1.wav"));
                expect (relativePaths (other.files, root).contains ("A/Reamped/a2_reamp.wav"));
                expectEquals ((int) other.skippedOutputFolders.size(), 1);

                // Case: macOS and Windows file systems ignore it, so does the skip.
                const auto upper = FolderScanner::scan ({ A }, true, formats, {}, "REAMPED");
                expectEquals ((int) upper.skippedOutputFolders.size(), juce::File::areFileNamesCaseSensitive() ? 0 : 2);

                // No name: everything is scanned (the old behaviour).
                const auto all = FolderScanner::scan ({ A }, true, formats);
                expectEquals ((int) all.files.size(), 7);
                expect (all.skippedOutputFolders.empty());

                // Recursion off: subfolders are not entered anyway, so none is reported.
                const auto flat = FolderScanner::scan ({ A }, false, formats, {}, "Reamped");
                expectEquals ((int) flat.files.size(), 2);
                expect (flat.skippedOutputFolders.empty());

                // The user adds the output folder itself: that is a choice, it is scanned.
                const auto direct = FolderScanner::scan ({ A.getChildFile ("Reamped") }, true, formats, {}, "Reamped");
                expectEquals (namesOf (direct.files).joinIntoString (","), juce::String ("a2_reamp.wav"));
                expect (direct.skippedOutputFolders.empty());

                for (const auto* sub : { "Reamped", "sub/Reamped", "sub/Reamped2" })
                    A.getChildFile (sub).deleteRecursively();
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

            testReviewFixes (tmp, A, B, formats);
        }

    private:
        static bool pumpUntil (const std::function<bool()>& done, int timeoutMs)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

            while (! done() && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

            return done();
        }

        // Review 2026-10-01: U8 (dedupe by file identity), U9 (output folder by path in
        // single-folder mode), U5 (the sanitized subfolder name), U10 (scanAsync, destruction).
        void testReviewFixes (const TempDirectory& tmp, const juce::File& A, const juce::File& B, juce::AudioFormatManager& formats)
        {
            beginTest ("dedupe by file identity: two differently-cased names");
            {
                const auto folder = tmp.folder ("Case");
                const auto upper = folder.getChildFile ("Kick.wav");
                const auto lower = folder.getChildFile ("kick.wav");
                expect (writeWav (upper, 1, 48000.0, 24, 4800));

                // Detected at runtime: on a case-insensitive volume "kick.wav" is "Kick.wav".
                const auto caseInsensitiveVolume = lower.existsAsFile();

                if (caseInsensitiveVolume)
                {
                    logMessage ("    the temp volume is case-insensitive: one file, two spellings (the case-sensitive "
                                "assertion is skipped)");
                    const auto r = FolderScanner::scan ({ upper, lower }, false, formats);
                    expectEquals ((int) r.files.size(), 1, "the same file under two spellings is listed once");

                    FileTree tree;
                    expectEquals (tree.add (r.files).added, 1);
                    const auto again = tree.add (FolderScanner::scan ({ lower }, false, formats).files);
                    expectEquals (again.added, 0);
                    expectEquals (again.duplicates, 1);
                    expect (tree.contains (lower) && tree.contains (upper));
                }
                else
                {
                    expect (writeWav (lower, 2, 44100.0, 16, 4410));
                    const auto r = FolderScanner::scan ({ folder }, false, formats);
                    expectEquals ((int) r.files.size(), 2, "two files on a case-sensitive volume");

                    FileTree tree;
                    expectEquals (tree.add (r.files).added, 2, "both are listed");
                    expectEquals ((int) tree.getGroups().size(), 1);
                }

                // A path to the same file with "." and ".." in it is the same file too.
                FileTree tree;
                tree.add (FolderScanner::scan ({ upper }, false, formats).files);
                const auto viaDots = folder.getChildFile ("../Case/./Kick.wav");
                expect (tree.contains (viaDots));

                folder.deleteRecursively();
            }

            beginTest ("single output folder inside the source tree: skipped by path, whatever its name");
            {
                expect (writeWav (A.getChildFile ("Bounces/a2_reamp.wav"), 1, 48000.0, 24, 480));
                expect (writeWav (A.getChildFile ("Reamped/real source.wav"), 1, 48000.0, 24, 480));

                // Single-folder mode: the app passes the output folder and no subfolder name.
                const auto r = FolderScanner::scan ({ A }, true, formats, {}, {}, A.getChildFile ("Bounces"));
                const auto paths = relativePaths (r.files, tmp.get());
                expect (! paths.contains ("A/Bounces/a2_reamp.wav"), "outputs are not rescanned");
                expect (paths.contains ("A/Reamped/real source.wav"), "a source folder that happens to be called Reamped is scanned");
                expectEquals ((int) r.skippedOutputFolders.size(), 1);

                if (! r.skippedOutputFolders.empty())
                    expect (r.skippedOutputFolders[0] == A.getChildFile ("Bounces"));

                // Subfolder mode: by name (the old rule), the output folder path is not passed.
                const auto byName = FolderScanner::scan ({ A }, true, formats, {}, "Reamped");
                expect (relativePaths (byName.files, tmp.get()).contains ("A/Bounces/a2_reamp.wav"));
                expect (! relativePaths (byName.files, tmp.get()).contains ("A/Reamped/real source.wav"));

                // An output folder that does not exist yet skips nothing.
                const auto none = FolderScanner::scan ({ A }, true, formats, {}, {}, A.getChildFile ("Not yet"));
                expect (none.skippedOutputFolders.empty());

                // Added directly, it is scanned (a choice).
                const auto direct = FolderScanner::scan ({ A.getChildFile ("Bounces") }, true, formats, {}, {}, A.getChildFile ("Bounces"));
                expectEquals ((int) direct.files.size(), 1);

                A.getChildFile ("Bounces").deleteRecursively();
                A.getChildFile ("Reamped").deleteRecursively();
            }

            beginTest ("subfolder names with # are skipped under the name used on disk");
            {
                expect (writeWav (A.getChildFile ("Reamped #2/a2_reamp.wav"), 1, 48000.0, 24, 480));

                NamingOptions o;
                o.subfolderName = " Reamped #2 ";
                const auto r = FolderScanner::scan ({ A }, true, formats, {}, OutputNaming::subfolderName (o));
                expect (! relativePaths (r.files, tmp.get()).contains ("A/Reamped #2/a2_reamp.wav"));
                expectEquals ((int) r.skippedOutputFolders.size(), 1);

                A.getChildFile ("Reamped #2").deleteRecursively();
            }

            beginTest ("scanAsync: results on the message thread, in the order queued");
            {
                FolderScanner scanner;
                std::vector<int> order;
                std::vector<int> counts;
                auto allOnMessageThread = true;

                for (int i = 0; i < 2; ++i)
                {
                    scanner.scanAsync (i == 0 ? juce::Array<juce::File> { A } : juce::Array<juce::File> { B }, i == 0,
                                       [&, i] (ScanResult r)
                                       {
                                           allOnMessageThread = allOnMessageThread && juce::MessageManager::existsAndIsCurrentThread();
                                           order.push_back (i);
                                           counts.push_back ((int) r.files.size());
                                       });
                }

                expectEquals (scanner.getNumPending(), 2);
                expect (pumpUntil ([&] { return order.size() == 2; }, 10000), "both scans delivered");
                expect (order == std::vector<int> { 0, 1 });
                expect (counts == std::vector<int> { 4, 1 });
                expect (allOnMessageThread);
                expectEquals (scanner.getNumPending(), 0);
            }

            beginTest ("scanAsync: destroying the scanner mid-scan delivers nothing afterwards");
            {
                auto delivered = 0;

                {
                    auto scanner = std::make_unique<FolderScanner>();

                    for (int i = 0; i < 4; ++i)
                        scanner->scanAsync ({ A, B }, true, [&delivered] (ScanResult) { ++delivered; });

                    scanner.reset();     // stops its thread; results already posted are dropped
                }

                pumpUntil ([] { return false; }, 300);
                expectEquals (delivered, 0);
            }
        }
    };

    static FolderScannerTests folderScannerTests;
}
