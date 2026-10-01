#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace rf::engine
{
    /** One channel of a source file, decoded into memory for real-time playback. Immutable
        once loaded; shared between the message thread and (read-only) the audio thread. */
    struct LoadedSource
    {
        juce::File file;
        int channel = 0;                    // channel of the file that was decoded (0 = L)
        double fileSampleRate = 0.0;
        juce::int64 fileLengthInSamples = 0;

        double sampleRate = 0.0;            // rate of `samples` (the device rate when resampled)
        juce::AudioBuffer<float> samples;   // one channel
        bool resampled = false;

        float peak = 0.0f;                  // absolute peak of the file's channel (linear, before gain)

        int getNumSamples() const noexcept  { return samples.getNumSamples(); }
    };

    struct LoadRequest
    {
        juce::File file;
        int channel = 0;
        double targetSampleRate = 0.0;      // 0 = keep the file's rate
    };

    /*  Decodes the chosen channel of a file into memory and measures its peak, on the loader
        thread (ARCHITECTURE.md). Used for the audition preview and the "resulting peak" shown
        next to the output level, and by the batch (its own instance) to load the current
        file and preload the next one while a take records.

        If the target rate differs from the file's rate the channel is resampled with the
        high-quality Resampler (aligned at sample 0) so it can be played at the device rate;
        the batch uses the same path, so a resampled take plays exactly what audition plays.
    */
    class SourceLoader
    {
    public:
        SourceLoader();
        ~SourceLoader();

        /** Synchronous load, thread-agnostic (used by the tests). `shouldAbort` is polled
            between chunks. Returns null and sets `error` on failure or abort. */
        static std::shared_ptr<const LoadedSource> load (const LoadRequest&, juce::AudioFormatManager&,
                                                         juce::String& error,
                                                         const std::function<bool()>& shouldAbort = {});

        using Callback = std::function<void (std::shared_ptr<const LoadedSource>, juce::String error)>;

        /** Loads on the background thread and calls `onDone` on the message thread. A new
            request cancels the previous one; only the latest request's result is delivered. */
        void loadAsync (LoadRequest, Callback onDone);

        /** Cancels the pending request (its callback is not called). */
        void cancel();

        bool isLoading() const noexcept     { return pendingRequest != 0; }

    private:
        struct Job;

        juce::AudioFormatManager formats;   // used on the loader thread only
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
        juce::uint32 lastRequest = 0;       // message thread only
        juce::uint32 pendingRequest = 0;    // message thread only; 0 = nothing pending

        // Declared last so it is destroyed (and its thread stopped) before the members above.
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Source loader")
                                                        .withNumberOfThreads (1) };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourceLoader)
    };
}
