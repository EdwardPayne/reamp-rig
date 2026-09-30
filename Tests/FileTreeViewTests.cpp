#include "Model/FileTree.h"
#include "UI/FileTreeView.h"
#include "UI/LookAndFeel.h"

namespace rf::test
{
    using namespace rf::model;

    /*  Drives the file list's keyboard handling headlessly (no window): cmd-A, L / R on a
        mixed selection, Up / Down with and without shift, and Backspace / Delete.
    */
    class FileTreeViewTests final : public juce::UnitTest
    {
    public:
        FileTreeViewTests() : juce::UnitTest ("FileTreeView keyboard", "FileTreeView") {}

        void runTest() override
        {
            beginTest ("setup");

            ui::ForgeLookAndFeel lookAndFeel;
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

           #if JUCE_WINDOWS
            const juce::String base ("C:\\di\\");
           #else
            const juce::String base ("/di/");
           #endif

            FileTree tree;
            std::vector<ScannedFile> files;

            for (const auto& [name, channels] : { std::pair<const char*, int> { "a mono.wav", 1 },
                                                  { "b stereo.wav", 2 }, { "c stereo.wav", 2 }, { "d mono.wav", 1 } })
            {
                ScannedFile s;
                s.file = juce::File (base + name);
                s.info.numChannels = channels;
                s.info.sampleRate = 48000.0;
                s.info.bitsPerSample = 24;
                s.info.lengthInSamples = 48000;
                files.push_back (s);
            }

            expectEquals (tree.add (files).added, 4);

            {
                ui::FileTreeView view (tree);
                view.setSize (900, 400);
                auto& list = view.getListComponent();
                const auto ids = tree.getAllIds();
                const auto press = [&list] (const juce::KeyPress& k) { return list.keyPressed (k); };

                beginTest ("cmd-A selects all");
                expect (press (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 'a')));
                expectEquals (tree.getNumSelected(), 4);

                beginTest ("R sets the right channel on selected stereo files only");
                expect (press (juce::KeyPress ('r', {}, 'r')));
                expect (tree.find (ids[1])->channel == Channel::right);
                expect (tree.find (ids[2])->channel == Channel::right);
                expect (tree.find (ids[0])->channel == Channel::left);
                expect (tree.find (ids[3])->channel == Channel::left);

                beginTest ("L sets it back");
                expect (press (juce::KeyPress ('l', {}, 'l')));
                expect (tree.find (ids[1])->channel == Channel::left);

                beginTest ("Down moves the lead, shift-Down extends");
                tree.selectOnly (ids[0]);
                expect (press (juce::KeyPress (juce::KeyPress::downKey)));
                expectEquals ((int) tree.getLead(), (int) ids[1]);
                expectEquals (tree.getNumSelected(), 1);
                expect (press (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
                expectEquals ((int) tree.getLead(), (int) ids[2]);
                expectEquals (tree.getNumSelected(), 2);

                beginTest ("Backspace removes the selection and selects the next file");
                expect (press (juce::KeyPress (juce::KeyPress::backspaceKey)));
                expectEquals (tree.getNumFiles(), 2);
                expectEquals ((int) tree.getLead(), (int) ids[3]);
                expect (tree.isSelected (ids[3]));

                beginTest ("Delete removes too");
                expect (press (juce::KeyPress (juce::KeyPress::deleteKey)));
                expectEquals (tree.getNumFiles(), 1);

                beginTest ("unhandled keys fall through");
                expect (! press (juce::KeyPress ('x', {}, 'x')));
            }

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }
    };

    static FileTreeViewTests fileTreeViewTests;
}
