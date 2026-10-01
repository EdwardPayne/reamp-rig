#include "Model/FileTree.h"
#include "UI/ConfirmDialog.h"
#include "UI/FileTreeView.h"
#include "UI/LookAndFeel.h"

namespace rf::test
{
    using namespace rf::model;

    /*  Keyboard shortcuts in every state (PROMPT.md section 5, phase 6), headless:

        - the confirmation dialog takes Return (confirm) and Escape (cancel) and swallows every
          other key (Space, Delete, L, R) so nothing reaches the list or audition behind it;
          command shortcuts pass on to the application;
        - the file list keeps cmd-A and L / R working while a batch runs (the window-level
          Space handler refuses audition then; that path is covered by code review: it only
          shows a status-bar message);
        - collapsing a folder group deselects its files, and cmd-A selects the visible files
          only, so bulk actions never reach hidden rows (decision 2026-10-01).
    */
    class KeyboardTests final : public juce::UnitTest
    {
    public:
        KeyboardTests() : juce::UnitTest ("Keyboard shortcuts in every state", "Keyboard") {}

        void runTest() override
        {
            beginTest ("setup");

            ui::ForgeLookAndFeel lookAndFeel;
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            testDialog();
            testListDuringBatchAndCollapse();

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

    private:
        void testDialog()
        {
            ui::ConfirmDialog dialog;
            dialog.setSize (1280, 800);

            std::optional<bool> answer;
            auto show = [&]
            {
                answer.reset();
                dialog.show ({ "Title", "Intro", { "Item" }, "Note", "Start anyway", "Cancel" },
                             [&answer] (bool ok) { answer = ok; });
            };

            beginTest ("dialog: other keys are swallowed and change nothing");
            show();
            expect (dialog.isShowing());

            for (const auto& k : { juce::KeyPress (juce::KeyPress::spaceKey), juce::KeyPress (juce::KeyPress::deleteKey),
                                   juce::KeyPress (juce::KeyPress::backspaceKey), juce::KeyPress ('l', {}, 'l'),
                                   juce::KeyPress ('r', {}, 'r'), juce::KeyPress (juce::KeyPress::downKey) })
                expect (dialog.keyPressed (k), k.getTextDescription());

            expect (dialog.isShowing());
            expect (! answer.has_value());

            beginTest ("dialog: command shortcuts pass on (cmd-Q quits even while it shows)");
            expect (! dialog.keyPressed (juce::KeyPress ('q', juce::ModifierKeys::commandModifier, 'q')));
            expect (! dialog.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 'a')));
            expect (dialog.isShowing());

            beginTest ("dialog: Escape cancels");
            expect (dialog.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (! dialog.isShowing());
            expect (answer == std::optional<bool> (false));

            beginTest ("dialog: Return confirms");
            show();
            expect (dialog.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expect (! dialog.isShowing());
            expect (answer == std::optional<bool> (true));

            beginTest ("dialog: hidden, it takes no keys");
            expect (! dialog.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
            expect (! dialog.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expect (answer == std::optional<bool> (true));
        }

        void testListDuringBatchAndCollapse()
        {
           #if JUCE_WINDOWS
            const juce::String a ("C:\\di\\A\\"), b ("C:\\di\\B\\");
           #else
            const juce::String a ("/di/A/"), b ("/di/B/");
           #endif

            FileTree tree;
            std::vector<ScannedFile> files;

            for (const auto& path : { a + "a1.wav", a + "a2.wav", b + "b1.wav", b + "b2.wav" })
            {
                ScannedFile s;
                s.file = juce::File (path);
                s.info.numChannels = 2;
                s.info.sampleRate = 48000.0;
                s.info.bitsPerSample = 24;
                s.info.lengthInSamples = 48000;
                files.push_back (s);
            }

            expectEquals (tree.add (files).added, 4);
            const auto ids = tree.getAllIds();

            ui::FileTreeView view (tree);
            view.setSize (900, 400);
            auto& list = view.getListComponent();
            const auto press = [&list] (const juce::KeyPress& k) { return list.keyPressed (k); };
            const auto cmdA = juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 'a');

            beginTest ("batch running: cmd-A and L / R still work on the list");
            view.setBatchState (ids[0], true);
            expect (press (cmdA));
            expectEquals (tree.getNumSelected(), 4);
            expect (press (juce::KeyPress ('r', {}, 'r')));
            expect (tree.find (ids[3])->channel == Channel::right);
            expect (press (juce::KeyPress ('l', {}, 'l')));
            expect (tree.find (ids[3])->channel == Channel::left);
            view.setBatchState (0, false);

            beginTest ("collapsing a group deselects its files; cmd-A then selects the visible files only");
            expect (press (cmdA));
            expectEquals (tree.getNumSelected(), 4);

            // Click the header of group B (row 3: A header, a1, a2, B header).
            constexpr int rowHeight = 28;
            const juce::Point<float> groupB (40.0f, 3.5f * rowHeight);
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), groupB, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &list, &list, now, groupB, now, 1, false);
            list.mouseDown (click);

            expectEquals (tree.getNumSelected(), 2);
            expect (tree.isSelected (ids[0]) && tree.isSelected (ids[1]));
            expect (! tree.isSelected (ids[2]) && ! tree.isSelected (ids[3]));

            expect (press (cmdA));
            expectEquals (tree.getNumSelected(), 2, "hidden files are not selected by cmd-A");

            expect (press (juce::KeyPress ('r', {}, 'r')));
            expect (tree.find (ids[2])->channel == Channel::left, "R never reaches a hidden file");

            expect (press (juce::KeyPress (juce::KeyPress::deleteKey)));
            expectEquals (tree.getNumFiles(), 2, "Delete removes only the visible selection");
            expect (tree.find (ids[2]) != nullptr && tree.find (ids[3]) != nullptr);

            beginTest ("expanding the group again shows its files unselected");
            const juce::Point<float> groupBNow (40.0f, 0.5f * rowHeight);   // A is empty now: B is the first row
            const juce::MouseEvent click2 (juce::Desktop::getInstance().getMainMouseSource(), groupBNow, {}, 1.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, &list, &list, now, groupBNow, now, 1, false);
            list.mouseDown (click2);
            expect (press (cmdA));
            expectEquals (tree.getNumSelected(), 2);
        }
    };

    static KeyboardTests keyboardTests;
}
