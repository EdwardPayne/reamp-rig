#include "BatchQueue.h"

#include <algorithm>

namespace rf::model
{
    int BatchQueue::start (const FileTree& tree)
    {
        std::vector<Entry> queued;

        for (const auto& group : tree.getGroups())
            for (const auto& item : group.files)
                if (item.status == FileStatus::queued)
                    queued.push_back ({ item.id, item.info.sampleRate, item.info.lengthInSamples, FileStatus::queued });

        return start (std::move (queued));
    }

    int BatchQueue::start (std::vector<Entry> newEntries)
    {
        entries = std::move (newEntries);

        for (auto& e : entries)
            e.outcome = FileStatus::queued;

        current = -1;
        state = entries.empty() ? State::idle : State::running;
        return (int) entries.size();
    }

    ItemId BatchQueue::advance()
    {
        if (state != State::running)
            return 0;

        for (int i = current + 1; i < (int) entries.size(); ++i)
        {
            if (entries[(size_t) i].outcome == FileStatus::queued)
            {
                current = i;
                return entries[(size_t) i].id;
            }
        }

        current = -1;
        state = State::finished;
        return 0;
    }

    ItemId BatchQueue::getCurrent() const noexcept
    {
        const auto* e = getCurrentEntry();
        return e != nullptr ? e->id : 0;
    }

    const BatchQueue::Entry* BatchQueue::getCurrentEntry() const noexcept
    {
        return juce::isPositiveAndBelow (current, (int) entries.size()) ? &entries[(size_t) current] : nullptr;
    }

    const BatchQueue::Entry* BatchQueue::peekNext() const noexcept
    {
        for (int i = current + 1; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].outcome == FileStatus::queued)
                return &entries[(size_t) i];

        return nullptr;
    }

    void BatchQueue::finishCurrent (FileStatus outcome)
    {
        jassert (outcome == FileStatus::done || outcome == FileStatus::skipped || outcome == FileStatus::error);

        if (juce::isPositiveAndBelow (current, (int) entries.size()))
            entries[(size_t) current].outcome = outcome;
    }

    void BatchQueue::pause()
    {
        if (state == State::running)
            state = State::paused;
    }

    void BatchQueue::resume()
    {
        if (state == State::paused)
            state = State::running;
    }

    void BatchQueue::stop()
    {
        if (isActive())
        {
            state = State::stopped;
            current = -1;
        }
    }

    void BatchQueue::remove (ItemId id)
    {
        for (int i = 0; i < (int) entries.size(); ++i)
        {
            if (entries[(size_t) i].id != id || i == current)
                continue;

            entries.erase (entries.begin() + i);

            if (i < current)
                --current;

            return;
        }
    }

    int BatchQueue::countWithOutcome (FileStatus s) const noexcept
    {
        return (int) std::count_if (entries.begin(), entries.end(), [s] (const Entry& e) { return e.outcome == s; });
    }

    bool BatchQueue::isGroupStart (int index) const noexcept
    {
        if (! juce::isPositiveAndBelow (index, (int) entries.size()))
            return false;

        return index == 0 || ! juce::approximatelyEqual (entries[(size_t) index].sampleRate,
                                                         entries[(size_t) index - 1].sampleRate);
    }

    std::vector<BatchQueue::Group> BatchQueue::getGroups() const
    {
        std::vector<Group> groups;

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            if (isGroupStart (i))
                groups.push_back ({ i, 0, entries[(size_t) i].sampleRate });

            ++groups.back().count;
        }

        return groups;
    }

    double BatchQueue::getRemainingSeconds (double extraSecondsPerFile, double currentElapsedSeconds) const
    {
        auto total = 0.0;

        for (int i = juce::jmax (0, current); i < (int) entries.size(); ++i)
            if (entries[(size_t) i].outcome == FileStatus::queued)
                total += entries[(size_t) i].getDurationSeconds() + extraSecondsPerFile;

        if (getCurrentEntry() != nullptr && getCurrentEntry()->outcome == FileStatus::queued)
            total -= juce::jmin (currentElapsedSeconds, getCurrentEntry()->getDurationSeconds() + extraSecondsPerFile);

        return juce::jmax (0.0, total);
    }
}
