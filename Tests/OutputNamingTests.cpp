#include "Model/OutputNaming.h"
#include "TestHelpers.h"

namespace rf::test
{
    using namespace rf::model;

    /*  Output names and folders (PROMPT.md 3.4 and section 7): prefix/suffix, channel tag,
        both destination modes including the mirrored structure, every collision policy, and
        the validation messages. Collisions run against real files in a temporary folder.
    */
    class OutputNamingTests final : public juce::UnitTest
    {
    public:
        OutputNamingTests() : juce::UnitTest ("OutputNaming", "OutputNaming") {}

        void expectSameFile (const juce::File& actual, const juce::File& expected)
        {
            expectEquals (actual.getFullPathName(), expected.getFullPathName());
        }

        void runTest() override
        {
            TempDirectory dir;
            const auto sessions = dir.folder ("DI");
            const auto sessionA = dir.folder ("DI/Session A");
            const auto takes = dir.folder ("DI/Session A/Takes");
            const auto riff = sessionA.getChildFile ("Riff 01.wav");
            const auto take = takes.getChildFile ("Take 02.aif");

            beginTest ("prefix, suffix and extension");
            {
                NamingOptions o;
                o.prefix = "amp_";
                o.suffix = "_reamp";
                expectEquals (OutputNaming::fileName (riff, o, std::nullopt), juce::String ("amp_Riff 01_reamp.wav"));

                o.prefix = {};
                o.suffix = {};
                expectEquals (OutputNaming::fileName (take, o, std::nullopt), juce::String ("Take 02.wav"),
                              "the output is always WAV");
            }

            beginTest ("channel tag only when on and only for sources with a channel choice");
            {
                NamingOptions o;
                expectEquals (OutputNaming::fileName (riff, o, Channel::right), juce::String ("Riff 01_reamp.wav"));

                o.channelTag = true;
                expectEquals (OutputNaming::fileName (riff, o, Channel::left), juce::String ("Riff 01_reamp_L.wav"));
                expectEquals (OutputNaming::fileName (riff, o, Channel::right), juce::String ("Riff 01_reamp_R.wav"));
                expectEquals (OutputNaming::fileName (riff, o, std::nullopt), juce::String ("Riff 01_reamp.wav"), "mono: no tag");
                expectEquals (OutputNaming::fileName (riff, o, Channel::left, 3), juce::String ("Riff 01_reamp_L (3).wav"));
            }

            beginTest ("subfolder next to the source");
            {
                NamingOptions o;
                expect (OutputNaming::validate (o).isEmpty());
                expectSameFile (OutputNaming::folder (riff, sessions, o), sessionA.getChildFile ("Reamped"));
                expectSameFile (OutputNaming::folder (take, sessions, o), takes.getChildFile ("Reamped"));
                expectEquals (OutputNaming::example (riff, sessions, std::nullopt, o), juce::String ("Reamped/Riff 01_reamp.wav"));

                o.subfolderName = "  Amped  ";
                expectSameFile (OutputNaming::folder (riff, sessions, o), sessionA.getChildFile ("Amped"));

                o.subfolderName = "a/b";
                expect (OutputNaming::validate (o).isNotEmpty());
                o.subfolderName = "..";
                expect (OutputNaming::validate (o).isNotEmpty());
                o.subfolderName = " ";
                expect (OutputNaming::validate (o).isNotEmpty());
            }

            beginTest ("single output folder, flat");
            {
                NamingOptions o;
                o.mode = DestinationMode::singleFolder;
                expect (OutputNaming::validate (o).isNotEmpty(), "no folder chosen yet");

                const auto out = dir.get().getChildFile ("Out");
                o.outputFolder = out;
                o.mirrorStructure = false;
                expect (OutputNaming::validate (o).isEmpty());
                expectSameFile (OutputNaming::folder (riff, sessions, o), out);
                expectSameFile (OutputNaming::folder (take, sessions, o), out);
                expectEquals (OutputNaming::example (take, sessions, std::nullopt, o), juce::String ("Out/Take 02_reamp.wav"));
            }

            beginTest ("single output folder, mirrored below the added folder");
            {
                NamingOptions o;
                o.mode = DestinationMode::singleFolder;
                o.outputFolder = dir.get().getChildFile ("Out");
                o.mirrorStructure = true;

                // The user added "DI": its name and everything below it is mirrored.
                expectSameFile (OutputNaming::folder (riff, sessions, o), o.outputFolder.getChildFile ("DI/Session A"));
                expectSameFile (OutputNaming::folder (take, sessions, o), o.outputFolder.getChildFile ("DI/Session A/Takes"));

                // The user added "Session A".
                expectSameFile (OutputNaming::folder (take, sessionA, o), o.outputFolder.getChildFile ("Session A/Takes"));
                expectEquals (OutputNaming::example (take, sessionA, std::nullopt, o),
                              juce::String ("Out/Session A/Takes/Take 02_reamp.wav"));

                // A single file was added (root = the file), or no root is known: its folder.
                expectSameFile (OutputNaming::folder (take, take, o), o.outputFolder.getChildFile ("Takes"));
                expectSameFile (OutputNaming::folder (take, juce::File(), o), o.outputFolder.getChildFile ("Takes"));

                // A root that does not contain the file is ignored.
                expectSameFile (OutputNaming::folder (riff, takes, o), o.outputFolder.getChildFile ("Session A"));
            }

            beginTest ("illegal characters are replaced, separators in prefix/suffix rejected");
            {
                NamingOptions o;
                o.suffix = "*?";
                const auto name = OutputNaming::fileName (riff, o, std::nullopt);
                expect (! name.containsAnyOf ("*?\"<>|"), name);

                o.suffix = "/x";
                expect (OutputNaming::validate (o).isNotEmpty());
            }

            beginTest ("collision: auto-number");
            {
                NamingOptions o;   // auto-number is the default
                expect (o.collision == CollisionPolicy::autoNumber);

                auto t = OutputNaming::resolve (riff, sessions, std::nullopt, o);
                expect (! t.skip && ! t.overwrites);
                expectSameFile (t.file, sessionA.getChildFile ("Reamped/Riff 01_reamp.wav"));

                expect (writeJunk (t.file));
                t = OutputNaming::resolve (riff, sessions, std::nullopt, o);
                expectEquals (t.file.getFileName(), juce::String ("Riff 01_reamp (2).wav"));

                expect (writeJunk (t.file));
                t = OutputNaming::resolve (riff, sessions, std::nullopt, o);
                expectEquals (t.file.getFileName(), juce::String ("Riff 01_reamp (3).wav"));
            }

            beginTest ("collision: overwrite");
            {
                NamingOptions o;
                o.collision = CollisionPolicy::overwrite;

                const auto t = OutputNaming::resolve (riff, sessions, std::nullopt, o);
                expectSameFile (t.file, sessionA.getChildFile ("Reamped/Riff 01_reamp.wav"));
                expect (t.overwrites && ! t.skip);

                o.suffix = "_fresh";
                expect (! OutputNaming::resolve (riff, sessions, std::nullopt, o).overwrites, "nothing to overwrite");
            }

            beginTest ("collision: skip");
            {
                NamingOptions o;
                o.collision = CollisionPolicy::skip;

                const auto t = OutputNaming::resolve (riff, sessions, std::nullopt, o);
                expect (t.skip);
                expect (t.file == juce::File());
                expect (t.reason.contains ("Riff 01_reamp.wav"));

                o.channelTag = true;
                expect (! OutputNaming::resolve (riff, sessions, Channel::left, o).skip, "a different name does not collide");
            }

            beginTest ("collision policies in the mirrored output folder");
            {
                NamingOptions o;
                o.mode = DestinationMode::singleFolder;
                o.outputFolder = dir.get().getChildFile ("Out");

                const auto first = OutputNaming::resolve (take, sessionA, std::nullopt, o);
                expectSameFile (first.file, o.outputFolder.getChildFile ("Session A/Takes/Take 02_reamp.wav"));
                expect (writeJunk (first.file));

                expectEquals (OutputNaming::resolve (take, sessionA, std::nullopt, o).file.getFileName(),
                              juce::String ("Take 02_reamp (2).wav"));

                o.collision = CollisionPolicy::skip;
                expect (OutputNaming::resolve (take, sessionA, std::nullopt, o).skip);

                o.collision = CollisionPolicy::overwrite;
                expect (OutputNaming::resolve (take, sessionA, std::nullopt, o).overwrites);
            }

            testReviewFixes (dir);
        }

    private:
        // Review 2026-10-01: U2 (one batch never overwrites its own outputs), U3 (long names),
        // U5 (only really illegal characters are removed), U6 (a volume root added as a whole).
        void testReviewFixes (const TempDirectory& dir)
        {
            beginTest ("legal names: only / \\ : * ? \" < > | and control characters are removed");
            {
                expectEquals (OutputNaming::legalName (juce::String ("Riff #2, take@home; v1/2:3*?\"<>|\\") + juce::String::charToString (9)
                                                       + juce::String::charToString (1)),
                              juce::String ("Riff #2, take@home; v123"));
                expectEquals (OutputNaming::legalName (juce::CharPointer_UTF8 ("Gitarre \xc3\xa9 & (Bass) [1] {x} ~ \xe6\xbc\xa2")),
                              juce::String (juce::CharPointer_UTF8 ("Gitarre \xc3\xa9 & (Bass) [1] {x} ~ \xe6\xbc\xa2")));

               #if JUCE_WINDOWS
                expectEquals (OutputNaming::legalName ("Take. . "), juce::String ("Take"), "Windows trims trailing dots and spaces");
               #else
                expectEquals (OutputNaming::legalName ("Take. "), juce::String ("Take. "), "kept where the file system keeps them");
               #endif

                NamingOptions o;
                const auto hash = dir.get().getChildFile ("DI/Session A/Riff #2, take@home.wav");
                expectEquals (OutputNaming::fileName (hash, o, std::nullopt), juce::String ("Riff #2, take@home_reamp.wav"),
                              "# @ , ; are legal: the name is not changed silently");

                o.prefix = "#1 ";
                o.suffix = "; amp@3";
                expectEquals (OutputNaming::fileName (hash, o, std::nullopt), juce::String ("#1 Riff #2, take@home; amp@3.wav"));
            }

            beginTest ("subfolder names: # is a real folder, the scanner gets the name used on disk");
            {
                const auto riff = dir.get().getChildFile ("DI/Session A/Riff 01.wav");
                NamingOptions o;

                o.subfolderName = "#";
                expect (OutputNaming::validate (o).isEmpty());
                expectEquals (OutputNaming::subfolderName (o), juce::String ("#"));
                expectSameFile (OutputNaming::folder (riff, {}, o), riff.getParentDirectory().getChildFile ("#"));
                expect (OutputNaming::folder (riff, {}, o) != riff.getParentDirectory(), "never the source folder itself");

                o.subfolderName = "Reamped #2";
                expectEquals (OutputNaming::subfolderName (o), juce::String ("Reamped #2"));
                expectSameFile (OutputNaming::folder (riff, {}, o), riff.getParentDirectory().getChildFile ("Reamped #2"));
                expectEquals (OutputNaming::example (riff, {}, std::nullopt, o), juce::String ("Reamped #2/Riff 01_reamp.wav"));

                o.subfolderName = " Amped*? ";
                expectEquals (OutputNaming::subfolderName (o), juce::String ("Amped"));

                o.subfolderName = "???";
                expect (OutputNaming::validate (o).isNotEmpty(), "nothing usable left");
                o.subfolderName = "*";
                expect (OutputNaming::validate (o).isNotEmpty());
            }

            beginTest ("long names: only the source name is shortened, the result fits the limit");
            {
                NamingOptions o;
                o.channelTag = true;
                const auto folder = dir.folder ("Long");
                const auto longSource = folder.getChildFile (juce::String::repeatedString ("a", 300) + ".wav");

                const auto first = OutputNaming::fileName (longSource, o, Channel::right);
                const auto second = OutputNaming::fileName (longSource, o, Channel::right, 2);
                const auto third = OutputNaming::fileName (longSource, o, Channel::right, 3);

                expectLessOrEqual ((int) first.getNumBytesAsUTF8(), OutputNaming::maxNameBytes);
                expectEquals ((int) second.getNumBytesAsUTF8(), OutputNaming::maxNameBytes);
                expect (first.endsWith ("_reamp_R.wav"), first);
                expect (second.endsWith ("_reamp_R (2).wav"), second);
                expect (third.endsWith ("_reamp_R (3).wav"), third);
                expect (second != third, "auto-numbering still produces different names");

                // Multi-byte characters are never cut in half.
                const auto accented = folder.getChildFile (juce::String::repeatedString (juce::CharPointer_UTF8 ("\xc3\xa9"), 200) + ".wav");
                const auto name = OutputNaming::fileName (accented, o, Channel::left, 12);
                expectLessOrEqual ((int) name.getNumBytesAsUTF8(), OutputNaming::maxNameBytes);
                expect (name.endsWith ("_reamp_L (12).wav"), name);
                expect (juce::CharPointer_UTF8::isValidString (name.toRawUTF8(), (int) name.getNumBytesAsUTF8()));

                // On disk: the name and its temp file can be created, and auto-number finds a free one.
                // Created like FileWriter does (an output stream on the name itself; juce's
                // replaceWithText would add a longer temporary name of its own).
                const auto create = [] (const juce::File& f)
                {
                    f.getParentDirectory().createDirectory();
                    juce::FileOutputStream stream (f);
                    return stream.openedOk() && stream.writeText ("x", false, false, nullptr);
                };

                auto t = OutputNaming::resolve (longSource, longSource, Channel::right, o);
                expect (create (t.file), t.file.getFileName());
                expect (create (t.file.getSiblingFile ("." + t.file.getFileNameWithoutExtension() + ".reamprig-part.wav")),
                        "the writer's hidden temp name fits too");
                t = OutputNaming::resolve (longSource, longSource, Channel::right, o);
                expect (! t.skip, t.reason);
                expect (t.file.getFileName().endsWith ("_reamp_R (2).wav"), t.file.getFileName());
            }

            beginTest ("one batch never overwrites its own output: same name from two folders, overwrite on");
            {
                const auto out = dir.get().getChildFile ("Batch Out");
                const auto a = dir.get().getChildFile ("Sessions/Session A/Riff 01.wav");
                const auto b = dir.get().getChildFile ("Sessions/Session B/Riff 01.wav");

                NamingOptions o;
                o.mode = DestinationMode::singleFolder;
                o.outputFolder = out;
                o.mirrorStructure = false;
                o.collision = CollisionPolicy::overwrite;

                OutputNaming::WrittenSet written;
                const auto first = OutputNaming::resolve (a, a.getParentDirectory(), std::nullopt, o, written);
                expectSameFile (first.file, out.getChildFile ("Riff 01_reamp.wav"));
                expect (first.note.isEmpty());
                expect (writeJunk (first.file));
                written.insert (first.file);

                const auto second = OutputNaming::resolve (b, b.getParentDirectory(), std::nullopt, o, written);
                expectSameFile (second.file, out.getChildFile ("Riff 01_reamp (2).wav"));
                expect (! second.overwrites && ! second.skip);
                expect (second.note.contains ("already written by this batch"), second.note);

                // A file from an earlier batch with the numbered name is overwritten (the policy),
                // the batch's own output never.
                expect (writeJunk (second.file));
                const auto again = OutputNaming::resolve (b, b.getParentDirectory(), std::nullopt, o, written);
                expectSameFile (again.file, out.getChildFile ("Riff 01_reamp (2).wav"));
                expect (again.overwrites);

                // A new batch (empty set): overwrite as before.
                const auto rerun = OutputNaming::resolve (a, a.getParentDirectory(), std::nullopt, o);
                expectSameFile (rerun.file, out.getChildFile ("Riff 01_reamp.wav"));
                expect (rerun.overwrites);
            }

            beginTest ("one batch never overwrites its own output: x.wav and x.aif, every policy");
            {
                const auto folder = dir.folder ("Formats");
                const auto wav = folder.getChildFile ("x.wav");
                const auto aif = folder.getChildFile ("x.aif");

                for (const auto policy : { CollisionPolicy::autoNumber, CollisionPolicy::overwrite, CollisionPolicy::skip })
                {
                    NamingOptions o;
                    o.collision = policy;
                    const auto subfolder = folder.getChildFile ("Reamped");
                    subfolder.deleteRecursively();

                    OutputNaming::WrittenSet written;
                    const auto first = OutputNaming::resolve (wav, folder, std::nullopt, o, written);
                    expect (writeJunk (first.file));
                    written.insert (first.file);

                    const auto second = OutputNaming::resolve (aif, folder, std::nullopt, o, written);
                    expect (! second.skip, "skip does not drop the second source of the same name");
                    expect (! second.overwrites);
                    expectSameFile (second.file, subfolder.getChildFile ("x_reamp (2).wav"));
                }

                // The set is compared like file names are (case-insensitive where they are).
                NamingOptions o;
                OutputNaming::WrittenSet written { folder.getChildFile ("Reamped/X_REAMP.wav") };
                const auto t = OutputNaming::resolve (wav, folder, std::nullopt, o, written);

                if (! juce::File::areFileNamesCaseSensitive())
                    expectEquals (t.file.getFileName(), juce::String ("x_reamp (2).wav"));
            }

            beginTest ("mirroring a volume root keeps the folders below it");
            {
               #if JUCE_WINDOWS
                const juce::File root ("C:\\");
                const juce::File a ("C:\\A\\x.wav"), b ("C:\\B\\x.wav"), top ("C:\\x.wav");
               #else
                const juce::File root ("/");
                const juce::File a ("/A/x.wav"), b ("/B/x.wav"), top ("/x.wav");
               #endif

                NamingOptions o;
                o.mode = DestinationMode::singleFolder;
                o.outputFolder = dir.get().getChildFile ("Out");

                expectSameFile (OutputNaming::folder (a, root, o), o.outputFolder.getChildFile ("A"));
                expectSameFile (OutputNaming::folder (b, root, o), o.outputFolder.getChildFile ("B"));
                expectSameFile (OutputNaming::folder (top, root, o), o.outputFolder);
            }
        }
    };

    static OutputNamingTests outputNamingTests;
}
