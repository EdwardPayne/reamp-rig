#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace rf::engine
{
    /*  The record FIFO of one take, between the audio thread and the writer thread
        (PROMPT.md 3.3.5, ARCHITECTURE.md "Data flow for one take").

        Single producer (the audio thread: write, writeSilence, markComplete, the counters)
        and single consumer (the writer thread: read). Lock-free: a juce::AbstractFifo over a
        buffer allocated once on the message thread. Nothing here allocates, locks or blocks.

        If the writer falls behind and the FIFO is full, the samples that do not fit are
        dropped and counted (getNumDropped); the take is then marked with a dropout warning.
        The same object carries the take's progress and the callback gaps the engine detected,
        so every per-take counter starts at zero with a fresh take.
    */
    class RecordStream
    {
    public:
        explicit RecordStream (int capacitySamples)
            : fifo (juce::jmax (2, capacitySamples) + 1),
              buffer ((size_t) juce::jmax (2, capacitySamples) + 1, true)
        {
        }

        //==============================================================================
        // Audio thread (producer)
        void write (const float* data, int numSamples) noexcept
        {
            int written = 0;

            {
                const auto scope = fifo.write (numSamples);   // published when it goes out of scope

                if (scope.blockSize1 > 0)
                    std::memcpy (buffer.get() + scope.startIndex1, data, (size_t) scope.blockSize1 * sizeof (float));

                if (scope.blockSize2 > 0)
                    std::memcpy (buffer.get() + scope.startIndex2, data + scope.blockSize1, (size_t) scope.blockSize2 * sizeof (float));

                written = scope.blockSize1 + scope.blockSize2;
            }

            account (numSamples, written);
        }

        void writeSilence (int numSamples) noexcept
        {
            int written = 0;

            {
                const auto scope = fifo.write (numSamples);

                if (scope.blockSize1 > 0)
                    juce::FloatVectorOperations::clear (buffer.get() + scope.startIndex1, scope.blockSize1);

                if (scope.blockSize2 > 0)
                    juce::FloatVectorOperations::clear (buffer.get() + scope.startIndex2, scope.blockSize2);

                written = scope.blockSize1 + scope.blockSize2;
            }

            account (numSamples, written);
        }

        /** Every sample of the take has been handed over (written or dropped). */
        void markComplete() noexcept                    { complete.store (true, std::memory_order_release); }

        void addCallbackGap() noexcept                  { gaps.fetch_add (1, std::memory_order_relaxed); }
        void setPosition (juce::int64 p) noexcept       { position.store (p, std::memory_order_relaxed); }

        //==============================================================================
        // Writer thread (consumer)
        int read (float* dest, int maxSamples) noexcept
        {
            const auto scope = fifo.read (maxSamples);   // released when it goes out of scope

            if (scope.blockSize1 > 0)
                std::memcpy (dest, buffer.get() + scope.startIndex1, (size_t) scope.blockSize1 * sizeof (float));

            if (scope.blockSize2 > 0)
                std::memcpy (dest + scope.blockSize1, buffer.get() + scope.startIndex2, (size_t) scope.blockSize2 * sizeof (float));

            return scope.blockSize1 + scope.blockSize2;
        }

        int getNumReady() const noexcept                { return fifo.getNumReady(); }

        //==============================================================================
        // Any thread
        bool isComplete() const noexcept                { return complete.load (std::memory_order_acquire); }
        juce::int64 getNumHandled() const noexcept      { return handled.load (std::memory_order_acquire); }
        juce::int64 getNumDropped() const noexcept      { return dropped.load (std::memory_order_relaxed); }
        int getCallbackGaps() const noexcept            { return gaps.load (std::memory_order_relaxed); }
        juce::int64 getPosition() const noexcept        { return position.load (std::memory_order_relaxed); }
        int getCapacity() const noexcept                { return fifo.getTotalSize() - 1; }

    private:
        void account (int wanted, int written) noexcept
        {
            if (written < wanted)
                dropped.fetch_add (wanted - written, std::memory_order_relaxed);

            handled.fetch_add (wanted, std::memory_order_release);
        }

        juce::AbstractFifo fifo;
        juce::HeapBlock<float> buffer;

        std::atomic<juce::int64> handled { 0 }, dropped { 0 }, position { 0 };
        std::atomic<int> gaps { 0 };
        std::atomic<bool> complete { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordStream)
    };
}
