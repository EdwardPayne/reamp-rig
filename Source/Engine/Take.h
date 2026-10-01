#pragma once

#include "DuplexEngine.h"
#include "FileWriter.h"

namespace rf::engine
{
    /** Everything one take needs (PROMPT.md 3.3.2, 4.3, 4.4). */
    struct TakeSpec
    {
        std::shared_ptr<const LoadedSource> source;     // the played channel, at the device rate
        double deviceRate = 0.0;
        int latencySamples = 0;                         // round trip in device samples
        bool latencyMeasured = false;                   // false: the driver-reported estimate
        juce::int64 tailSamples = 0;                    // extra recording after the source, file rate
        juce::File outputFile;                          // final name
        int bitsPerSample = 24;                         // 16, 24 or 32 (float)
        bool replaceExisting = false;
        double fifoSeconds = 4.0;                       // record FIFO capacity

        /** Writer thread: every block written to the file (for the recorded thumbnail). */
        std::function<void (juce::int64 start, const float* data, int numSamples)> onWritten;
    };

    struct TakeResult
    {
        bool ok = false;
        bool cancelled = false;
        juce::String error;
        juce::File file;
        juce::int64 length = 0;             // samples in the file
        float peak = 0.0f;                  // of the recording, linear
        bool silent = false;                // peak below -60 dBFS (PROMPT.md 3.3.7)
        bool clipped = false;               // peak at full scale
        bool resampled = false;             // device ran at another rate (4.4)
        bool notCalibrated = false;         // latency was an estimate (3.6.4)
        juce::int64 droppedSamples = 0;     // record FIFO overflow
        int callbackGaps = 0;
        juce::int64 paddedSamples = 0;
        bool writeFailed = false;           // the destination refused the file (see WriteResult)

        bool hadDropout() const noexcept    { return droppedSamples > 0 || callbackGaps > 0 || paddedSamples > 0; }
    };

    /*  One take: plays a source and records the input into a file, sample-aligned
        (ARCHITECTURE.md, "Data flow for one take").

        start() sizes the record FIFO, starts the FileWriter on its thread and hands the source
        and the FIFO to the DuplexEngine. Lengths:

            same rate:  capture  sourceLength + latency + tail          (device = file rate)
                        discard  latency, write sourceLength + tail
            resampled:  capture  ceil (sourceLength * d / f) + latency + ceil (tail * d / f)
                                 + the resampler's half width (so the last samples see real input)
                        discard  latency (device samples), resample d -> f, write sourceLength + tail

        update() is called with each engine snapshot (message thread): once every sample has
        been captured it stops the engine's take, then waits for the writer's result.
        cancel() stops both and deletes the temporary file.
    */
    class Take
    {
    public:
        /** -60 dBFS: below this the recording is reported as "Recorded silence?". */
        static constexpr float silenceThreshold = 0.001f;

        enum class Phase { idle, recording, writing, done };

        Take() = default;
        ~Take();

        /** Samples captured by the engine (device rate). */
        static juce::int64 getRecordLength (const TakeSpec&);

        /** Samples in the file (file rate): source length + tail. */
        static juce::int64 getOutputLength (const TakeSpec&);

        /** Returns an error message, or an empty string when the take is running. */
        juce::String start (TakeSpec, DuplexEngine&, FileWriter&);

        Phase update (const EngineSnapshot&);
        void cancel();

        Phase getPhase() const noexcept                 { return phase; }

        /** 0..1 over capture and writing. */
        double getProgress() const noexcept;

        /** Playback position in seconds of the source (stops at the source's end). */
        double getPlayheadSeconds() const noexcept;

        const TakeSpec& getSpec() const noexcept        { return spec; }
        const TakeResult& getResult() const noexcept    { return result; }

    private:
        TakeSpec spec;
        DuplexEngine* engine = nullptr;
        FileWriter* writer = nullptr;
        Phase phase = Phase::idle;
        const RecordStream* stream = nullptr;    // this take's FIFO (matches EngineSnapshot::takeStream)
        juce::int64 recordLength = 0, position = 0;
        TakeResult result;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Take)
    };
}
