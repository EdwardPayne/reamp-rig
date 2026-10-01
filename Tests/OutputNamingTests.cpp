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
        }
    };

    static OutputNamingTests outputNamingTests;
}
