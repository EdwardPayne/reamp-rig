#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "AudioDeviceInterface.h"

#include <atomic>

namespace rf::engine
{
    /*  A simulated audio interface whose selected output channel is cabled back into its
        selected input channel (PROMPT.md section 4.5). No hardware, no permission prompt.

        Loop model: the callback's output block is played while the next block is processed
        (as on real hardware), so a sample written at output stream position m reaches the
        input stream at position m + bufferSize + delay, scaled by `gain`, plus uniform noise
        of +-`noise` if set. The round trip is therefore getRoundTripSamples() =
        bufferSize + delay; the driver latency it reports adds up to exactly that (unless
        `reportedInputLatency` / `reportedOutputLatency` are set), so the "not calibrated"
        estimate is right for this device. With `loop` off every input is silent.

        Other input channels carry a constant 0.25 (tests prove the engine records the right
        channel); every output buffer is pre-filled with 0.5 before the callback (tests prove
        unused outputs are zeroed). Like Tests/FakeAudioDevice.h it passes all channels to the
        callback, and the layout indices are the channel indices.

        Two ways to drive it:
        - render (numSamples): synchronous, block by block on the calling thread (tests);
          returns what the callback wrote to every output channel.
        - paced: open() starts its own audio thread that calls the callback in real time
          (or `speed` times faster). Used by the app's --virtual-device development mode.

        streamTime() is a simulated clock (seconds of audio rendered plus injected gaps) for
        DuplexEngine::setClock, so tests can provoke a callback gap with addTimeGap().
    */
    class LoopbackTestDevice final : public AudioDeviceInterface,
                                     private juce::Thread
    {
    public:
        struct Options
        {
            juce::String typeName = "Loopback";
            juce::String deviceName = "Loopback Interface";
            juce::StringArray inputs { "In 1", "In 2" };
            juce::StringArray outputs { "Out 1", "Out 2", "Out 3", "Out 4" };
            juce::Array<double> sampleRates { 44100.0, 48000.0, 96000.0 };
            juce::Array<int> bufferSizes { 64, 128, 256, 480, 512, 1024 };
            double defaultSampleRate = 48000.0;
            int defaultBufferSize = 256;

            bool loop = true;
            int delay = 0;                      // samples on top of one buffer
            float gain = 1.0f;
            float noise = 0.0f;                 // uniform noise amplitude added to the loop
            int reportedInputLatency = -1;      // -1: report the real round trip
            int reportedOutputLatency = -1;

            bool paced = false;                 // run an own real-time thread after open()
            double speed = 1.0;                 // paced: how much faster than real time
        };

        explicit LoopbackTestDevice (Options);
        ~LoopbackTestDevice() override;

        const Options& getOptions() const noexcept   { return options; }

        /** Changes the loop for the next open() (tests). */
        void setLoop (bool loop, int delay, float gain, float noise);

        int getRoundTripSamples() const noexcept;

        //==============================================================================
        juce::StringArray getTypeNames() override;
        juce::StringArray getDeviceNames (const juce::String& typeName, bool inputs) override;
        juce::String getDefaultDeviceName (const juce::String& typeName, bool input) override;
        bool hasSeparateInputsAndOutputs (const juce::String&) override   { return false; }

        juce::String open (const DeviceConfig&) override;
        void close() override;
        DeviceStatus getStatus() override;
        void setCallback (DuplexCallback*) override;

        //==============================================================================
        /** Tests: runs `numSamples` through the callback in blocks of the buffer size (the last
            block may be shorter) and returns every output channel. Not for paced mode. */
        juce::AudioBuffer<float> render (int numSamples);

        /** Simulated stream clock in seconds (for DuplexEngine::setClock). */
        double streamTime() const noexcept;

        /** Tests: the next callback arrives `seconds` later than it should (a gap). */
        void addTimeGap (double seconds) noexcept;

        /** Tests: adds to the xrun count the device reports in its status. */
        void simulateXrun() noexcept                  { ++xruns; }

        bool isRunning() const noexcept               { return running; }

        /** Simulates unplugging (false) and plugging the interface back in (true), message
            thread: unplugged, the stream stops, the device is not listed, open() fails and the
            status says "device disconnected"; listeners are told either way. For the
            reconnection checks (--virtual-unplug). */
        void setPresent (bool shouldBePresent);
        bool isPresent() const noexcept               { return present; }

    private:
        void run() override;
        void startStream();
        void stopStream();
        void renderBlock (int numSamples);

        Options options;
        DeviceStatus status;
        DuplexCallback* callback = nullptr;
        bool running = false;
        bool present = true;

        // Preallocated in open(); used by the thread that renders.
        juce::AudioBuffer<float> inputs, outputs;
        std::vector<float> line;            // delay line of the looped output, by stream position
        juce::int64 sampleCounter = 0;
        juce::Random random { 7 };

        std::atomic<double> gapSeconds { 0.0 };
        std::atomic<int> xruns { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackTestDevice)
    };
}
