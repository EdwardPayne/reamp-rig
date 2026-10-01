#include "LoopbackTestDevice.h"

#include <chrono>
#include <thread>

namespace rf::engine
{
    namespace
    {
        template <typename T>
        T nearest (const juce::Array<T>& values, T wanted)
        {
            auto best = values.getFirst();

            for (auto v : values)
                if (std::abs ((double) v - (double) wanted) < std::abs ((double) best - (double) wanted))
                    best = v;

            return best;
        }

        constexpr float otherInputValue = 0.25f;
        constexpr float outputGarbage = 0.5f;
    }

    LoopbackTestDevice::LoopbackTestDevice (Options o)
        : juce::Thread ("Loopback audio device"), options (std::move (o))
    {
    }

    LoopbackTestDevice::~LoopbackTestDevice()
    {
        stopStream();
    }

    void LoopbackTestDevice::setLoop (bool loop, int delay, float gain, float noise)
    {
        options.loop = loop;
        options.delay = juce::jmax (0, delay);
        options.gain = gain;
        options.noise = noise;
    }

    int LoopbackTestDevice::getRoundTripSamples() const noexcept
    {
        const auto buffer = status.isOpen ? status.config.bufferSize : options.defaultBufferSize;
        return buffer + options.delay;
    }

    //==============================================================================
    juce::StringArray LoopbackTestDevice::getTypeNames()
    {
        return { options.typeName };
    }

    juce::StringArray LoopbackTestDevice::getDeviceNames (const juce::String& type, bool)
    {
        return type == options.typeName && present ? juce::StringArray (options.deviceName) : juce::StringArray();
    }

    void LoopbackTestDevice::setPresent (bool shouldBePresent)
    {
        if (present == shouldBePresent)
            return;

        present = shouldBePresent;

        if (! present && status.isOpen)
        {
            stopStream();
            status = {};
            status.lastError = "device disconnected";
        }

        notifyListeners();
    }

    juce::String LoopbackTestDevice::getDefaultDeviceName (const juce::String& type, bool)
    {
        return type == options.typeName ? options.deviceName : juce::String();
    }

    juce::String LoopbackTestDevice::open (const DeviceConfig& config)
    {
        stopStream();

        if (! present || config.typeName != options.typeName
            || (config.outputDevice != options.deviceName && config.inputDevice != options.deviceName))
            return "No such device";

        const auto rate = nearest (options.sampleRates, config.sampleRate > 0.0 ? config.sampleRate : options.defaultSampleRate);

        if (options.rejectedRates.contains (rate))
        {
            // Like a driver that lists a rate but cannot run at it: the device ends up closed.
            status = {};
            status.lastError = "the device refused " + juce::String (rate, 0) + " Hz";
            notifyListeners();
            return status.lastError;
        }

        status = {};
        status.config = config;
        status.config.inputDevice = status.config.outputDevice = options.deviceName;
        status.config.sampleRate = nearest (options.sampleRates, config.sampleRate > 0.0 ? config.sampleRate
                                                                                         : options.defaultSampleRate);
        status.config.bufferSize = nearest (options.bufferSizes, config.bufferSize > 0 ? config.bufferSize
                                                                                       : options.defaultBufferSize);

        if (! juce::isPositiveAndBelow (config.inputChannel, options.inputs.size()))
            status.config.inputChannel = -1;

        if (! juce::isPositiveAndBelow (config.outputChannel, options.outputs.size()))
            status.config.outputChannel = -1;

        status.inputChannelNames = options.inputs;
        status.outputChannelNames = options.outputs;
        status.config.inputChannelName = options.inputs[status.config.inputChannel];
        status.config.outputChannelName = options.outputs[status.config.outputChannel];
        status.sampleRates = options.sampleRates;
        status.bufferSizes = options.bufferSizes;

        const auto roundTrip = status.config.bufferSize + options.delay;
        status.inputLatencySamples = options.reportedInputLatency >= 0 ? options.reportedInputLatency : roundTrip / 2;
        status.outputLatencySamples = options.reportedOutputLatency >= 0 ? options.reportedOutputLatency
                                                                         : roundTrip - roundTrip / 2;
        status.isOpen = true;

        const auto block = status.config.bufferSize;
        inputs.setSize (juce::jmax (1, options.inputs.size()), block);
        outputs.setSize (juce::jmax (1, options.outputs.size()), block);

        // Delay line: enough for the round trip plus one block, a power of two for masking.
        line.assign ((size_t) juce::nextPowerOfTwo (roundTrip + 2 * block + 1), 0.0f);
        sampleCounter = 0;
        gapSeconds.store (0.0);

        startStream();
        notifyListeners();
        return {};
    }

    void LoopbackTestDevice::close()
    {
        stopStream();
        status = {};
        notifyListeners();
    }

    DeviceStatus LoopbackTestDevice::getStatus()
    {
        auto s = status;
        s.xrunCount = status.isOpen ? xruns.load() : -1;
        return s;
    }

    void LoopbackTestDevice::setCallback (DuplexCallback* cb)
    {
        stopStream();
        callback = cb;
        startStream();
    }

    void LoopbackTestDevice::startStream()
    {
        if (callback == nullptr || ! status.isOpen)
            return;

        StreamLayout layout;
        layout.sampleRate = status.config.sampleRate;
        layout.bufferSize = status.config.bufferSize;
        layout.inputIndex = status.config.inputChannel;     // all channels are passed
        layout.outputIndex = status.config.outputChannel;
        callback->streamStarting (layout);
        running = true;

        if (options.paced)
            startThread (juce::Thread::Priority::highest);
    }

    void LoopbackTestDevice::stopStream()
    {
        stopThread (2000);

        if (running && callback != nullptr)
            callback->streamStopped();

        running = false;
    }

    //==============================================================================
    double LoopbackTestDevice::streamTime() const noexcept
    {
        const auto rate = status.config.sampleRate > 0.0 ? status.config.sampleRate : 48000.0;
        return (double) sampleCounter / rate + gapSeconds.load (std::memory_order_relaxed);
    }

    void LoopbackTestDevice::addTimeGap (double seconds) noexcept
    {
        gapSeconds.store (gapSeconds.load() + seconds);
    }

    void LoopbackTestDevice::renderBlock (int n)
    {
        const auto numIns = options.inputs.size();
        const auto numOuts = options.outputs.size();
        const auto inChannel = status.config.inputChannel;
        const auto outChannel = status.config.outputChannel;
        const auto mask = (juce::int64) line.size() - 1;
        const auto roundTrip = (juce::int64) status.config.bufferSize + options.delay;
        const auto looping = options.loop && juce::isPositiveAndBelow (outChannel, numOuts);

        // Input: the looped output from `roundTrip` samples ago (always an earlier block).
        for (int ch = 0; ch < numIns; ++ch)
        {
            auto* in = inputs.getWritePointer (ch);

            if (ch != inChannel)
            {
                juce::FloatVectorOperations::fill (in, options.loop ? otherInputValue : 0.0f, n);
                continue;
            }

            for (int i = 0; i < n; ++i)
            {
                const auto source = sampleCounter + i - roundTrip;
                auto v = looping && source >= 0 ? line[(size_t) (source & mask)] * options.gain : 0.0f;

                if (looping && options.noise > 0.0f)
                    v += (random.nextFloat() * 2.0f - 1.0f) * options.noise;

                in[i] = v;
            }
        }

        for (int ch = 0; ch < numOuts; ++ch)
            juce::FloatVectorOperations::fill (outputs.getWritePointer (ch), outputGarbage, n);

        if (callback != nullptr && running)
            callback->process (inputs.getArrayOfReadPointers(), numIns, outputs.getArrayOfWritePointers(), numOuts, n);

        if (looping)
        {
            const auto* out = outputs.getReadPointer (outChannel);

            for (int i = 0; i < n; ++i)
                line[(size_t) ((sampleCounter + i) & mask)] = out[i];
        }

        sampleCounter += n;
    }

    juce::AudioBuffer<float> LoopbackTestDevice::render (int numSamples)
    {
        jassert (! options.paced);

        const auto numOuts = options.outputs.size();
        const auto block = juce::jmax (1, status.config.bufferSize);

        juce::AudioBuffer<float> result (juce::jmax (1, numOuts), numSamples);
        result.clear();

        if (! status.isOpen)
            return result;

        for (int pos = 0; pos < numSamples; pos += block)
        {
            const auto n = juce::jmin (block, numSamples - pos);
            renderBlock (n);

            for (int ch = 0; ch < numOuts; ++ch)
                result.copyFrom (ch, pos, outputs, ch, 0, n);
        }

        return result;
    }

    void LoopbackTestDevice::run()
    {
        // Paced like a real device: one block every bufferSize / sampleRate seconds (divided
        // by `speed`). Falls back to real time when it gets behind instead of bursting.
        using clock = std::chrono::steady_clock;
        const auto blockDuration = std::chrono::duration<double> ((double) status.config.bufferSize
                                                                  / status.config.sampleRate / juce::jmax (0.01, options.speed));
        auto next = clock::now();

        while (! threadShouldExit())
        {
            renderBlock (status.config.bufferSize);

            next += std::chrono::duration_cast<clock::duration> (blockDuration);
            const auto now = clock::now();

            if (next > now)
                std::this_thread::sleep_until (next);
            else
                next = now;
        }
    }
}
