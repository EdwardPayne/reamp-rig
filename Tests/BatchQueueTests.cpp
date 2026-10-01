#include "Model/BatchQueue.h"

namespace rf::test
{
    using namespace rf::model;

    namespace
    {
        ScannedFile scanned (const juce::String& path, double rate, juce::int64 length)
        {
            ScannedFile f;
            f.file = juce::File (path);
            f.info.numChannels = 1;
            f.info.sampleRate = rate;
            f.info.bitsPerSample = 24;
            f.info.lengthInSamples = length;
            f.root = f.file.getParentDirectory();
            return f;
        }

        std::vector<ItemId> runAll (BatchQueue& q, FileStatus outcome = FileStatus::done)
        {
            std::vector<ItemId> order;

            for (auto id = q.advance(); id != 0; id = q.advance())
            {
                order.push_back (id);
                q.finishCurrent (outcome);
            }

            return order;
        }
    }

    /*  Batch order and state (PROMPT.md 3.3.1, 4.4): list order, only Queued files, done
        files skipped until reset, pause/resume/stop/skip, sample-rate groups, ETA input.
    */
    class BatchQueueTests final : public juce::UnitTest
    {
    public:
        BatchQueueTests() : juce::UnitTest ("BatchQueue", "BatchQueue") {}

        void runTest() override
        {
            FileTree tree;
            const auto added = tree.add ({ scanned ("/a/1.wav", 48000.0, 48000), scanned ("/a/2.wav", 48000.0, 96000),
                                           scanned ("/a/3.wav", 44100.0, 44100), scanned ("/b/4.wav", 44100.0, 88200),
                                           scanned ("/b/5.wav", 48000.0, 24000) });
            const auto ids = added.addedIds;

            beginTest ("runs every queued file in list order, then finishes");
            {
                BatchQueue q;
                expect (q.getState() == BatchQueue::State::idle);
                expectEquals (q.start (tree), 5);
                expect (q.getState() == BatchQueue::State::running);
                expect (q.isActive());

                const auto order = runAll (q);
                expect (order == tree.getAllIds());
                expect (q.getState() == BatchQueue::State::finished);
                expectEquals ((int) q.getCurrent(), 0);
                expectEquals (q.countWithOutcome (FileStatus::done), 5);
            }

            beginTest ("done, skipped and error files are left out until reset");
            {
                tree.setStatus (ids[0], FileStatus::done, 1.0);
                tree.setStatus (ids[2], FileStatus::error, 0.0);
                tree.setStatus (ids[4], FileStatus::skipped, 0.0);

                BatchQueue q;
                expectEquals (q.start (tree), 2);
                expect (runAll (q) == std::vector<ItemId> { ids[1], ids[3] });

                tree.resetStatus ({ ids[0], ids[2], ids[4] });
                expectEquals (q.start (tree), 5);

                for (auto id : ids)
                    tree.setStatus (id, FileStatus::done, 1.0);

                expectEquals (q.start (tree), 0);
                expect (q.getState() == BatchQueue::State::idle, "nothing queued: does not start");
                expectEquals ((int) q.advance(), 0);

                tree.resetStatus (ids);
            }

            beginTest ("position, total and peekNext");
            {
                BatchQueue q;
                q.start (tree);
                expectEquals (q.getCurrentIndex(), -1);
                expectEquals ((int) q.advance(), (int) ids[0]);
                expectEquals (q.getCurrentIndex(), 0);
                expectEquals (q.getTotal(), 5);
                expect (q.peekNext() != nullptr && q.peekNext()->id == ids[1]);
                q.finishCurrent (FileStatus::done);
                q.advance();
                q.finishCurrent (FileStatus::done);
                q.advance();
                q.advance();
                q.advance();
                expect (q.peekNext() == nullptr);
            }

            beginTest ("pause keeps the current file, resume continues with it");
            {
                BatchQueue q;
                q.start (tree);
                expectEquals ((int) q.advance(), (int) ids[0]);
                q.finishCurrent (FileStatus::done);
                expectEquals ((int) q.advance(), (int) ids[1]);

                q.pause();
                expect (q.getState() == BatchQueue::State::paused);
                expect (q.isActive());
                expectEquals ((int) q.advance(), 0, "no progress while paused");
                expectEquals ((int) q.getCurrent(), (int) ids[1]);

                q.resume();
                expect (q.getState() == BatchQueue::State::running);
                expectEquals ((int) q.getCurrent(), (int) ids[1], "the interrupted file is redone");
                q.finishCurrent (FileStatus::done);
                expectEquals ((int) q.advance(), (int) ids[2]);
            }

            beginTest ("skip finishes the current file as skipped and moves on");
            {
                BatchQueue q;
                q.start (tree);
                q.advance();
                q.finishCurrent (FileStatus::skipped);
                expectEquals ((int) q.advance(), (int) ids[1]);
                expectEquals (q.countWithOutcome (FileStatus::skipped), 1);
            }

            beginTest ("stop ends the run");
            {
                BatchQueue q;
                q.start (tree);
                q.advance();
                q.stop();
                expect (q.getState() == BatchQueue::State::stopped);
                expect (! q.isActive());
                expectEquals ((int) q.getCurrent(), 0);
                expectEquals ((int) q.advance(), 0);
            }

            beginTest ("a file removed from the list leaves the run");
            {
                BatchQueue q;
                q.start (tree);
                q.advance();
                q.remove (ids[2]);
                expectEquals (q.getTotal(), 4);
                q.finishCurrent (FileStatus::done);
                expectEquals ((int) q.advance(), (int) ids[1]);
                q.finishCurrent (FileStatus::done);
                expectEquals ((int) q.advance(), (int) ids[3]);

                q.remove (ids[3]);   // the current one stays (the caller finishes it)
                expectEquals ((int) q.getCurrent(), (int) ids[3]);
            }

            beginTest ("sample-rate groups of consecutive files");
            {
                BatchQueue q;
                q.start (tree);
                const auto groups = q.getGroups();
                expectEquals ((int) groups.size(), 3);
                expectEquals (groups[0].first, 0);
                expectEquals (groups[0].count, 2);
                expectEquals (groups[0].sampleRate, 48000.0);
                expectEquals (groups[1].first, 2);
                expectEquals (groups[1].count, 2);
                expectEquals (groups[1].sampleRate, 44100.0);
                expectEquals (groups[2].first, 4);
                expectEquals (groups[2].count, 1);

                expect (q.isGroupStart (0) && ! q.isGroupStart (1) && q.isGroupStart (2) && ! q.isGroupStart (3)
                        && q.isGroupStart (4));
                expect (! q.isGroupStart (5));
            }

            beginTest ("remaining seconds for the ETA");
            {
                BatchQueue q;
                q.start (tree);
                // 1 + 2 + 1 + 2 + 0.5 seconds, plus 0.1 s per file
                expectWithinAbsoluteError (q.getRemainingSeconds (0.1, 0.0), 7.0, 1.0e-9);

                q.advance();
                expectWithinAbsoluteError (q.getRemainingSeconds (0.1, 0.5), 6.5, 1.0e-9);

                q.finishCurrent (FileStatus::done);
                q.advance();
                expectWithinAbsoluteError (q.getRemainingSeconds (0.1, 0.0), 5.9, 1.0e-9);
            }
        }
    };

    static BatchQueueTests batchQueueTests;
}
