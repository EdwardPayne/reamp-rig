#pragma once

#include "FileTree.h"

namespace rf::model
{
    /*  The order and state of one batch run (PROMPT.md section 3.3).

        start() takes every Queued file of the list, top to bottom in list order; Done,
        Skipped and Error files are left out until their status is reset. Files added or reset
        while the batch runs are not picked up by that run.

        The batch moves through the entries with advance(); each entry ends as done, skipped
        or error (finishCurrent). Pause keeps the current entry, which restarts from its
        beginning on resume; stop ends the run. Consecutive entries with the same sample rate
        form a group (PROMPT.md 4.4): the device rate is switched only at group starts.

        Pure state, no threads, no audio: the app drives it from the message thread.
    */
    class BatchQueue
    {
    public:
        enum class State { idle, running, paused, finished, stopped };

        struct Entry
        {
            ItemId id = 0;
            double sampleRate = 0.0;
            juce::int64 lengthInSamples = 0;
            FileStatus outcome = FileStatus::queued;    // queued until finished

            double getDurationSeconds() const noexcept   { return sampleRate > 0.0 ? (double) lengthInSamples / sampleRate : 0.0; }
        };

        /** Collects the Queued files of the list and starts running. Returns how many were
            queued; with none the state stays idle. */
        int start (const FileTree&);

        /** Same from explicit entries (tests). */
        int start (std::vector<Entry>);

        State getState() const noexcept                 { return state; }
        bool isActive() const noexcept                  { return state == State::running || state == State::paused; }

        /** Moves to the next unfinished entry and returns its id, or 0 (state becomes
            finished) when none is left. While paused it returns 0 and does not move. */
        ItemId advance();

        /** The entry being processed, 0 if none. */
        ItemId getCurrent() const noexcept;
        const Entry* getCurrentEntry() const noexcept;

        /** 0-based position of the current entry in the run, -1 if none. */
        int getCurrentIndex() const noexcept           { return current; }
        int getTotal() const noexcept                  { return (int) entries.size(); }
        const std::vector<Entry>& getEntries() const noexcept   { return entries; }

        /** The entry after the current one that will run next (for preloading), or null. */
        const Entry* peekNext() const noexcept;

        /** Records the outcome of the current entry (done, skipped or error). The current
            entry stays current until advance(). */
        void finishCurrent (FileStatus outcome);

        void pause();
        void resume();
        void stop();

        /** A file was removed from the list: its entry is dropped from the run (unless it is
            the current one, which the caller finishes as skipped). */
        void remove (ItemId);

        int countWithOutcome (FileStatus) const noexcept;

        /** True when entry `index` starts a new sample-rate group (the first entry, or a rate
            different from the entry before it). */
        bool isGroupStart (int index) const noexcept;

        /** Consecutive runs of entries with the same rate: (first index, count, rate). */
        struct Group { int first = 0; int count = 0; double sampleRate = 0.0; };
        std::vector<Group> getGroups() const;

        /** Seconds of audio still to process: the unfinished entries (each plus
            `extraSecondsPerFile`, e.g. tail and latency) minus `currentElapsedSeconds` of the
            current entry. */
        double getRemainingSeconds (double extraSecondsPerFile, double currentElapsedSeconds) const;

        /** Unfinished entries after the current one: each of them starts after a pause between
            files (phase 6), so the ETA adds one pause per entry. */
        int countUnfinishedAfterCurrent() const noexcept;

    private:
        std::vector<Entry> entries;
        int current = -1;
        State state = State::idle;
    };
}
