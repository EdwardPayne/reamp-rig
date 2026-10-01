#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "RecordStream.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace rf::engine
{
    /** What the writer thread turns one take's recording into. */
    struct WriteJob
    {
        std::shared_ptr<RecordStream> stream;
        juce::int64 discardSamples = 0;     // device-rate samples dropped from the start (the latency)
        juce::int64 outputLength = 0;       // samples in the file, at the file rate (source + tail)
        double deviceRate = 0.0;            // rate of the recording
        double fileRate = 0.0;              // rate of the file = the source's rate; resampled when different
        juce::File file;                    // final name; a temp file next to it is renamed on success
        int bitsPerSample = 24;             // 16 or 24 (PCM) or 32 (float)
        bool replaceExisting = false;       // CollisionPolicy::overwrite

        /** Called on the writer thread with every block written to the file (file rate,
            aligned with the source: sample 0 = source sample 0). For the recorded thumbnail. */
        std::function<void (juce::int64 start, const float* data, int numSamples)> onWritten;
    };

    struct WriteResult
    {
        bool ok = false;
        bool cancelled = false;
        juce::String error;
        juce::File file;
        juce::int64 samplesWritten = 0;     // == outputLength on success
        juce::int64 paddedSamples = 0;      // silence appended because samples were missing (dropouts)
        float peak = 0.0f;                  // absolute peak of the written samples
        bool resampled = false;
    };

    /*  The writer thread (PROMPT.md 3.3.5, 3.4.4, 3.4.6, 4.3, 4.4; ARCHITECTURE.md).

        Drains one take's RecordStream: drops the first `discardSamples` (latency
        compensation), resamples back to the file rate when the device ran at another rate
        (Resampler, aligned at sample 0), and writes exactly `outputLength` samples to a WAV
        file at the file's rate. Integer formats are written with an exact float -> int
        conversion (round to nearest, the inverse of how JUCE reads PCM), so a recording that
        equals the source comes back bit-exact. The file is written under a hidden temporary
        name in the destination folder and renamed when complete: an interrupted take never
        leaves a half-written file under the final name.

        One job at a time. start() / cancel() / getResult() are message-thread calls; the
        work happens on the writer's own thread, which polls the FIFO every couple of
        milliseconds (the audio thread never signals anything).
    */
    class FileWriter final : private juce::Thread
    {
    public:
        FileWriter();
        ~FileWriter() override;

        /** Creates the destination folder and the temp file and starts writing. Returns an
            error message (and starts nothing) if the file cannot be created. */
        juce::String start (WriteJob);

        /** Stops writing and deletes the temp file. */
        void cancel();

        bool isBusy() const noexcept                    { return busy.load(); }

        /** The result once the job has finished (successfully or not); empty while busy. */
        std::optional<WriteResult> getResult();

        /** Samples written to the file so far (file rate). */
        juce::int64 getSamplesWritten() const noexcept  { return written.load (std::memory_order_relaxed); }

        /** The hidden temporary file used while writing `finalFile`. */
        static juce::File getTempFile (const juce::File& finalFile);

        /** Converts float samples to left-justified 32-bit ints for a `bits`-bit PCM file:
            round (x * 2^(bits-1)), clamped, shifted up. Exactly inverts JUCE's PCM reading. */
        static void floatToPcm (const float* in, int* out, int numSamples, int bits) noexcept;

    private:
        void run() override;
        void writeSamples (const float* data, int numSamples);
        bool finish();

        WriteJob job;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::File tempFile;

        struct Work;
        std::unique_ptr<Work> work;     // writer thread only while busy

        std::atomic<bool> busy { false };
        std::atomic<juce::int64> written { 0 };

        std::mutex resultLock;          // never on the audio thread
        std::optional<WriteResult> result;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileWriter)
    };
}
