#pragma once

#include "DuplexEngine.h"
#include "SyncMeasurement.h"

#include <memory>
#include <optional>
#include <vector>

namespace rf::engine
{
    /** What to measure with (PROMPT.md 3.6.2). */
    struct SyncOptions
    {
        float levelDb = -12.0f;             // peak level of the test signal at the output, dBFS
        int repeats = 5;
        double recordSeconds = 1.0;         // recording per repeat (longer if latencyHint needs it)
        int latencyHint = 0;                // driver-reported round trip, samples (only sizes the recording)
    };

    enum class SyncFailure { none, silent, noPeak, tooLow, clipped, unstable, dropout, deviceStopped, cancelled };

    /** One repeat: the recording correlated with the emitted signal. */
    struct SyncRepeat
    {
        enum class Outcome { ok, silent, noPeak, tooLow, clipped, dropout };

        Outcome outcome = Outcome::noPeak;
        int delay = -1;                     // lag of the correlation peak, samples
        float recordingPeak = 0.0f;         // linear peak of the whole recording
        float returnedPeak = 0.0f;          // linear peak where the test signal came back
        double peakToSidelobeDb = 0.0;      // correlation peak over the largest value outside the main lobe
        bool inverted = false;              // the peak is negative (polarity flipped on the way)
    };

    struct SyncResult
    {
        bool ok = false;
        SyncFailure failure = SyncFailure::none;
        juce::String summary;               // short, for the SYNC section ("nothing came back")
        juce::String message;               // plain-language explanation with what to do
        SyncMeasurement measurement;        // valid when ok
        std::vector<SyncRepeat> repeats;    // every repeat that ran, in order
        double sampleRate = 0.0;
        bool inverted = false;
    };

    /*  Measures the round-trip latency of the current output -> input loop (PROMPT.md 3.6,
        ARCHITECTURE.md "Sync measurement").

        Each repeat plays a known test signal (a one-sample click, 5 ms of silence, then a
        50 ms exponential sine sweep from 200 Hz to 20 kHz or 0.45 of the rate, Hann-faded over
        2 ms, peak at `levelDb`) through DuplexEngine::startTake from an in-memory LoadedSource
        at the device rate, at an engine gain of 0 dB (restored afterwards), and records about
        1 s of the input into a RecordStream. The measurer's worker thread drains that stream
        (no FileWriter, no file) and cross-correlates the recording with the emitted signal
        (FFT, juce::dsp::FFT); the lag of the largest |correlation| is the round trip in
        samples, exactly the latency a take discards. Five repeats; delays further than one
        sample from the median are outliers; at least three must agree; the result is the
        median of those that do. Failures (nothing came back, no clear peak, too quiet,
        clipped, not repeatable, dropouts, device stopped) are reported in plain language and
        never produce a measurement.

        Message thread: start / update (with every engine snapshot, like engine::Take) /
        cancel. The audio thread only runs the engine's normal take path.
    */
    class SyncMeasurer
    {
    public:
        //==============================================================================
        // Thresholds (documented in ARCHITECTURE.md)
        static constexpr double sweepStartHz = 200.0;
        static constexpr double sweepEndHz = 20000.0;       // at most 0.45 of the rate
        static constexpr double sweepSeconds = 0.050;
        static constexpr double clickGapSeconds = 0.005;
        static constexpr double fadeSeconds = 0.002;

        static constexpr float silenceDb = -90.0f;          // recording peak below: nothing came back
        static constexpr double minPeakToSidelobeDb = 8.0;  // below: no clear peak
        static constexpr float minReturnedDb = -60.0f;      // returned signal below: too quiet
        static constexpr int tolerance = 1;                 // repeats within +-1 sample agree
        static constexpr int minAgreeing = 3;
        static constexpr double highPeakToSidelobeDb = 20.0;
        static constexpr double mediumPeakToSidelobeDb = 12.0;
        static constexpr double stallSeconds = 2.0;         // no progress: the device stopped

        enum class Phase { idle, measuring, analysing, done };

        SyncMeasurer();
        ~SyncMeasurer();

        /** Starts the first repeat at the device rate. Returns an error message, or an empty
            string when running. Replaces a running audition or take. */
        juce::String start (const SyncOptions&, double deviceRate, DuplexEngine&);

        /** Feed every engine snapshot (message thread). Returns the phase after it. */
        Phase update (const EngineSnapshot&);

        /** Stops the measurement; the result says "cancelled". */
        void cancel();

        Phase getPhase() const noexcept                 { return phase; }
        bool isRunning() const noexcept                 { return phase == Phase::measuring || phase == Phase::analysing; }
        int getRepeatsDone() const noexcept             { return (int) repeats.size(); }
        int getRepeatsRequested() const noexcept        { return options.repeats; }

        /** 0..1 over all repeats. */
        double getProgress() const noexcept;

        /** Valid once the phase is done. */
        const SyncResult& getResult() const noexcept    { return result; }

        //==============================================================================
        // Pure functions (any thread): the signal, one repeat's analysis, the combination.

        /** The emitted test signal at `rate`, peak = levelDb. */
        static std::vector<float> makeTestSignal (double rate, float levelDb);

        /** Main-lobe half width excluded around the peak for the sidelobe search: two periods
            of the sweep's start frequency (10 ms). */
        static int getExclusionSamples (double rate);

        /** Correlates one recording with the emitted signal. */
        static SyncRepeat analyse (const float* recording, int numSamples, const std::vector<float>& signal, double rate);

        /** Outlier rejection, median, confidence, or the failure (and its message). */
        static SyncResult combine (const std::vector<SyncRepeat>&, int repeatsRequested, double rate);

        static juce::String describe (SyncRepeat::Outcome);

    private:
        class Worker;

        juce::String startRepeat();
        void finish (SyncResult);

        SyncOptions options;
        double rate = 0.0;
        DuplexEngine* engine = nullptr;
        float savedGainDb = 0.0f;
        std::shared_ptr<const LoadedSource> source;
        std::shared_ptr<const std::vector<float>> signal;
        juce::int64 recordLength = 0;

        Phase phase = Phase::idle;
        const RecordStream* stream = nullptr;       // the current repeat's FIFO
        juce::int64 position = 0;
        double lastProgressMs = 0.0;
        std::vector<SyncRepeat> repeats;
        SyncResult result;

        std::unique_ptr<Worker> worker;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncMeasurer)
    };
}
