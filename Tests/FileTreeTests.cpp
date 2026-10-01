#include "Model/FileTree.h"

namespace rf::test
{
    using namespace rf::model;

    namespace
    {
        ScannedFile fakeFile (const juce::String& path, int numChannels)
        {
            ScannedFile s;
            s.file = juce::File (path);
            s.info.numChannels = numChannels;
            s.info.sampleRate = 48000.0;
            s.info.bitsPerSample = 24;
            s.info.lengthInSamples = 48000;
            return s;
        }

        struct CountingListener final : FileTree::Listener
        {
            int calls = 0;
            void fileTreeChanged() override   { ++calls; }
        };
    }

    class FileTreeTests final : public juce::UnitTest
    {
    public:
        FileTreeTests() : juce::UnitTest ("FileTree", "FileTree") {}

        void runTest() override
        {
           #if JUCE_WINDOWS
            const juce::String base ("C:\\di\\");
           #else
            const juce::String base ("/di/");
           #endif

            beginTest ("add notifies listeners once per batch");

            FileTree tree;
            CountingListener listener;
            tree.addListener (&listener);

            const auto added = tree.add ({ fakeFile (base + "mono1.wav", 1),
                                           fakeFile (base + "stereo1.wav", 2),
                                           fakeFile (base + "stereo2.wav", 2),
                                           fakeFile (base + "stereo3.wav", 2),
                                           fakeFile (base + "quad.wav", 4) });
            expectEquals (added.added, 5);
            expectEquals (listener.calls, 1);

            const auto idOf = [&] (const juce::String& name)
            {
                const auto* item = tree.findByFile (juce::File (base + name));
                return item != nullptr ? item->id : ItemId (0);
            };

            const auto channelOf = [&] (const juce::String& name) { return tree.find (idOf (name))->channel; };

            beginTest ("defaults: queued, channel L, stereo has a channel choice, mono does not");
            for (const auto id : tree.getAllIds())
            {
                const auto* item = tree.find (id);
                expect (item->status == FileStatus::queued);
                expect (item->channel == Channel::left);
                expect (item->hasChannelChoice() == (item->info.numChannels >= 2));
            }

            expectEquals (tree.countWithStatus (FileStatus::queued), 5);

            beginTest ("L/R on a multi-selection applies to all selected stereo files only");
            {
                tree.setSelection ({ idOf ("mono1.wav"), idOf ("stereo1.wav"), idOf ("stereo3.wav") },
                                   idOf ("stereo1.wav"));
                expectEquals (tree.getNumSelected(), 3);
                expectEquals ((int) tree.getLead(), (int) idOf ("stereo1.wav"));

                const auto changed = tree.setChannelOfSelection (Channel::right);
                expectEquals (changed, 2);
                expect (channelOf ("stereo1.wav") == Channel::right);
                expect (channelOf ("stereo3.wav") == Channel::right);
                expect (channelOf ("stereo2.wav") == Channel::left, "unselected stereo file must not change");
                expect (channelOf ("mono1.wav") == Channel::left, "mono file must ignore the choice");

                expectEquals (tree.setChannelOfSelection (Channel::right), 0, "no-op when already R");

                // A multichannel file counts as stereo (first two channels are selectable).
                tree.toggleSelected (idOf ("quad.wav"));
                expectEquals (tree.setChannelOfSelection (Channel::right), 1);
                expect (channelOf ("quad.wav") == Channel::right);

                expectEquals (tree.setChannelOfSelection (Channel::left), 3);
                expect (channelOf ("quad.wav") == Channel::left);
                expect (channelOf ("stereo1.wav") == Channel::left);
                expect (channelOf ("stereo2.wav") == Channel::left);
            }

            beginTest ("a selection of only mono files changes nothing");
            {
                const auto before = listener.calls;
                tree.selectOnly (idOf ("mono1.wav"));
                expectEquals (tree.setChannelOfSelection (Channel::right), 0);
                expect (channelOf ("mono1.wav") == Channel::left);
                expectEquals (listener.calls, before + 1); // selection change only
            }

            beginTest ("selection order, toggle and select all");
            {
                tree.setSelection ({ idOf ("stereo3.wav"), idOf ("mono1.wav") }, idOf ("stereo3.wav"));
                const auto ids = tree.getSelectedIds();
                expectEquals ((int) ids.size(), 2);
                expect (ids[0] == idOf ("mono1.wav"), "selected ids come back in list order");

                tree.toggleSelected (idOf ("mono1.wav"));
                expect (! tree.isSelected (idOf ("mono1.wav")));
                expectEquals ((int) tree.getLead(), (int) idOf ("mono1.wav"));

                tree.selectAll();
                expectEquals (tree.getNumSelected(), 5);
            }

            beginTest ("deselect (a collapsed group): only those files leave the selection, the lead stays");
            {
                tree.setChannel ({ idOf ("stereo1.wav") }, Channel::left);
                tree.selectAll();
                const auto lead = tree.getLead();
                const auto before = listener.calls;

                expectEquals (tree.deselect ({ idOf ("stereo1.wav"), idOf ("stereo2.wav") }), 2);
                expectEquals (tree.getNumSelected(), 3);
                expect (! tree.isSelected (idOf ("stereo1.wav")) && ! tree.isSelected (idOf ("stereo2.wav")));
                expectEquals ((int) tree.getLead(), (int) lead);
                expectEquals (listener.calls, before + 1);

                // Nothing selected among them: no change, no notification.
                expectEquals (tree.deselect ({ idOf ("stereo1.wav"), 999999 }), 0);
                expectEquals (listener.calls, before + 1);

                // Bulk actions now leave the deselected (hidden) files alone.
                tree.setChannelOfSelection (Channel::right);
                expect (channelOf ("stereo1.wav") == Channel::left);
                tree.setChannelOfSelection (Channel::left);
            }

            beginTest ("status reset and removal of the selection");
            {
                expect (tree.setStatus (idOf ("stereo2.wav"), FileStatus::done, 1.0));
                expect (tree.setStatus (idOf ("stereo3.wav"), FileStatus::error, 0.4));
                expectEquals (tree.countWithStatus (FileStatus::queued), 3);

                tree.setSelection ({ idOf ("stereo2.wav"), idOf ("stereo3.wav"), idOf ("mono1.wav") }, idOf ("stereo2.wav"));
                expectEquals (tree.resetStatusOfSelected(), 2);
                expectEquals (tree.countWithStatus (FileStatus::queued), 5);
                expectEquals (tree.find (idOf ("stereo3.wav"))->progress, 0.0);

                tree.setSelection ({ idOf ("stereo1.wav"), idOf ("stereo2.wav") }, idOf ("stereo1.wav"));
                expectEquals (tree.removeSelected(), 2);
                expectEquals (tree.getNumFiles(), 3);
                expectEquals (tree.getNumSelected(), 0);
                expectEquals ((int) tree.getLead(), 0);
                expect (! tree.contains (juce::File (base + "stereo1.wav")));

                const auto readded = tree.add ({ fakeFile (base + "stereo1.wav", 2) });
                expectEquals (readded.added, 1, "a removed file can be added again");

                tree.selectAll();
                tree.removeSelected();
                expect (tree.getGroups().empty(), "empty groups are dropped");
            }

            tree.removeListener (&listener);
        }
    };

    static FileTreeTests fileTreeTests;
}
