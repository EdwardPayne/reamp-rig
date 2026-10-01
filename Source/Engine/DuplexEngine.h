#pragma once

#include "AudioDeviceInterface.h"
#include "RecordStream.h"
#include "SourceLoader.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace rf::engine
{
    /** What the GUI needs from the engine, read on a timer (message thread). */
    struct EngineSnapshot
    {
        bool running = false;                   // a stream is active
        bool auditioning = false;
        bool auditionFinished = false;          // reached the end of the source
        juce::int64 playheadSample = 0;         // in the loaded source's samples
        double sourceSampleRate = 0.0;

        float inputPeak = 0.0f;                 // linear peak since the previous poll
        float outputPeak = 0.0f;
        bool inputClipped = false;              // reached full scale since the previous poll
        bool outputClipped = false;

        // Take (phase 4)
        bool taking = false;
        bool takeFinished = false;              // every one of takeLength samples was captured
        juce::int64 takePosition = 0;           // samples since the take started (device rate)
        juce::int64 takeLength = 0;             // samples to capture: source + latency + tail
        juce::int64 takeDropped = 0;            // samples the record FIFO could not take
        int takeGaps = 0;                       // callback gaps detected during the take
        const RecordStream* takeStream = nullptr;   // identifies the take these fields belong to
    };

    /*  The single duplex callback (PROMPT.md section 4.2): audition (phase 3), the take
        (phase 4) and the level meters.

        Audio thread (process): zeroes every output channel, copies the preloaded source into
        the selected output channel with the output gain (ramped across a block when the gain
        changes), and measures the selected input and output channels. During a take it also
        pushes the selected input channel into the take's RecordStream (a lock-free FIFO):
        output and capture start in the same callback at source sample 0, and exactly
        `recordLength` samples (source + latency + tail, PROMPT.md 4.3) are captured; the
        writer thread discards the first `latency` of them. It also watches the time between
        callbacks and counts gaps (a callback arriving more than 1.75 buffers, and at least
        3 ms more than one buffer, after the previous one). It never allocates, locks, logs or
        touches files: the source is decoded into memory beforehand by the SourceLoader and
        handed over, with the FIFO, as an immutable command through an atomic pointer.

        Message thread: startAudition / startTake / stop... / setGainDb / poll. Audition and
        take share the one command slot, so starting one replaces the other. Commands that
        the audio thread may still be reading are retired and freed only after a later
        callback has completed (or the stream has stopped).
    */
    class DuplexEngine final : public DuplexCallback
    {
    public:
        DuplexEngine();
        ~DuplexEngine() override;

        /** Level at or above which a sample counts as clipped (0 dBFS within 24-bit rounding). */
        static constexpr float clipLevel = 0.9999f;

        //==============================================================================
        // Message thread
        /** Starts playing `source` from `startSample` (in the source's samples). Returns false
            if the source is empty or the start is past its end. */
        bool startAudition (std::shared_ptr<const LoadedSource>, juce::int64 startSample);
        void stopAudition();
        bool isAuditioning() const noexcept             { return current != nullptr && current->stream == nullptr; }
        const LoadedSource* getAuditionSource() const   { return isAuditioning() ? current->source.get() : nullptr; }

        /** Starts a take: plays `source` (already at the device rate) from sample 0 and
            captures `recordLength` samples of the input channel into `stream`, starting in the
            same callback. Replaces a running audition. Returns false for an empty source, a
            record length shorter than the source, or no stream. */
        bool startTake (std::shared_ptr<const LoadedSource>, juce::int64 recordLength,
                        std::shared_ptr<RecordStream>);
        void stopTake();
        bool isTaking() const noexcept                  { return current != nullptr && current->stream != nullptr; }

        void setGainDb (float db);
        float getGainDb() const noexcept                { return gainDb; }

        /** Seconds clock used to detect callback gaps (default: the high-resolution system
            clock). Tests pass the simulated device's stream time. Set while no stream runs. */
        void setClock (std::function<double()>);

        /** Reads and resets the meter peaks and clip flags; also frees retired commands. */
        EngineSnapshot poll();

        StreamLayout getLayout() const noexcept         { return layout; }

        //==============================================================================
        // DuplexCallback
        void streamStarting (const StreamLayout&) override;
        void process (const float* const* inputs, int numInputs,
                      float* const* outputs, int numOutputs, int numSamples) noexcept override;
        void streamStopped() override;

    private:
        struct Command
        {
            std::shared_ptr<const LoadedSource> source;
            juce::int64 startSample = 0;
            juce::uint32 generation = 0;

            // Take only (null for audition)
            std::shared_ptr<RecordStream> stream;
            juce::int64 recordLength = 0;
        };

        void stopCommand();

        void retire (std::unique_ptr<Command>);
        void releaseRetired();

        // Message thread
        std::unique_ptr<Command> current;
        std::vector<std::pair<std::unique_ptr<Command>, juce::uint64>> retired;
        juce::uint32 nextGeneration = 1;
        float gainDb = 0.0f;
        StreamLayout layout;

        // Shared with the audio thread
        std::atomic<const Command*> command { nullptr };
        std::atomic<float> targetGain { 1.0f };
        std::atomic<int> inputIndex { -1 }, outputIndex { -1 };
        std::atomic<double> streamRate { 0.0 };
        std::atomic<bool> restartTiming { true };
        std::atomic<juce::uint64> callbackCount { 0 };
        std::atomic<bool> streamRunning { false };
        std::atomic<juce::int64> playhead { 0 };
        std::atomic<juce::uint32> finishedGeneration { 0 };
        std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
        std::atomic<bool> inputClip { false }, outputClip { false };

        std::function<double()> clock;

        // Audio thread only
        const Command* active = nullptr;
        juce::int64 position = 0;
        float currentGain = 1.0f;
        double lastCallbackTime = -1.0;
        int lastBlockSize = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuplexEngine)
    };
}
