#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Engine/AudioDeviceInterface.h"

#include <cmath>
#include <vector>

namespace rf::test
{
    /*  A scriptable AudioDeviceInterface without hardware.

        Holds a list of driver types, each with devices that have named input/output channels,
        supported rates and buffer sizes. open() follows the same rules as the real device
        (nearest supported rate/buffer, only valid channels are opened). render() drives the
        callback like an audio thread would and captures what it wrote to every output channel.

        Unlike JuceAudioDevice it hands the callback *all* channels of the device (unopened
        ones included), so tests also prove the callback zeroes channels it does not use.
    */
    class FakeAudioDevice final : public engine::AudioDeviceInterface
    {
    public:
        struct Device
        {
            juce::String name;
            juce::StringArray inputs, outputs;          // channel names
            juce::Array<double> rates { 44100.0, 48000.0, 96000.0 };
            juce::Array<int> bufferSizes { 64, 128, 256, 480, 512, 1024 };
            double defaultRate = 48000.0;
            int defaultBufferSize = 512;
            bool failsToOpen = false;
        };

        struct Type
        {
            juce::String name;
            bool separateInputsAndOutputs = true;
            std::vector<Device> devices;
            juce::String defaultInput, defaultOutput;
        };

        std::vector<Type> types;
        std::vector<engine::DeviceConfig> openCalls;    // every open() request, in order
        int inputLatency = 32, outputLatency = 48;

        //==============================================================================
        juce::StringArray getTypeNames() override
        {
            juce::StringArray names;

            for (const auto& t : types)
                names.add (t.name);

            return names;
        }

        juce::StringArray getDeviceNames (const juce::String& typeName, bool inputs) override
        {
            juce::StringArray names;

            if (const auto* t = findType (typeName))
                for (const auto& d : t->devices)
                    if (! (inputs ? d.inputs : d.outputs).isEmpty())
                        names.add (d.name);

            return names;
        }

        juce::String getDefaultDeviceName (const juce::String& typeName, bool input) override
        {
            if (const auto* t = findType (typeName))
                return input ? t->defaultInput : t->defaultOutput;

            return {};
        }

        bool hasSeparateInputsAndOutputs (const juce::String& typeName) override
        {
            const auto* t = findType (typeName);
            return t != nullptr && t->separateInputsAndOutputs;
        }

        juce::String open (const engine::DeviceConfig& config) override
        {
            openCalls.push_back (config);
            stopStream();

            // Like the real device, a direction without a channel is not opened at all.
            const auto inputName  = config.inputChannel  >= 0 ? config.inputDevice  : juce::String();
            const auto outputName = config.outputChannel >= 0 ? config.outputDevice : juce::String();

            if (inputName.isEmpty() && outputName.isEmpty())
                return "No channels to open";

            const auto* in  = findDevice (config.typeName, inputName);
            const auto* out = findDevice (config.typeName, outputName);

            if ((inputName.isNotEmpty() && in == nullptr) || (outputName.isNotEmpty() && out == nullptr))
                return "No such device";

            if ((in != nullptr && in->failsToOpen) || (out != nullptr && out->failsToOpen))
            {
                status = {};
                return "Device is busy";
            }

            const auto* primary = out != nullptr ? out : in;

            status = {};
            status.config = config;
            status.inputChannelNames = in != nullptr ? in->inputs : juce::StringArray();
            status.outputChannelNames = out != nullptr ? out->outputs : juce::StringArray();
            status.sampleRates = primary->rates;
            status.bufferSizes = primary->bufferSizes;

            status.config.sampleRate = nearest (primary->rates, config.sampleRate > 0.0 ? config.sampleRate : primary->defaultRate);
            status.config.bufferSize = nearest (primary->bufferSizes, config.bufferSize > 0 ? config.bufferSize
                                                                                            : primary->defaultBufferSize);

            if (! juce::isPositiveAndBelow (config.inputChannel, status.inputChannelNames.size()))
                status.config.inputChannel = -1;

            if (! juce::isPositiveAndBelow (config.outputChannel, status.outputChannelNames.size()))
                status.config.outputChannel = -1;

            status.config.inputChannelName = status.inputChannelNames[status.config.inputChannel];
            status.config.outputChannelName = status.outputChannelNames[status.config.outputChannel];
            status.inputLatencySamples = inputLatency;
            status.outputLatencySamples = outputLatency;
            status.isOpen = true;

            startStream();
            notifyListeners();
            return {};
        }

        void close() override
        {
            stopStream();
            status = {};
            notifyListeners();
        }

        engine::DeviceStatus getStatus() override   { return status; }

        void setCallback (engine::DuplexCallback* cb) override
        {
            stopStream();
            callback = cb;
            startStream();
        }

        //==============================================================================
        /** Runs `numSamples` through the callback in blocks of the open buffer size (the last
            block may be shorter). `input` fills the open input channel (all other inputs get
            noise). Outputs are pre-filled with garbage; returns every output channel. */
        juce::AudioBuffer<float> render (int numSamples, const std::function<float (juce::int64)>& input = {})
        {
            const auto numIns = status.inputChannelNames.size();
            const auto numOuts = status.outputChannelNames.size();
            const auto block = juce::jmax (1, status.config.bufferSize);

            juce::AudioBuffer<float> result (juce::jmax (1, numOuts), numSamples);
            result.clear();

            juce::AudioBuffer<float> ins (juce::jmax (1, numIns), block), outs (juce::jmax (1, numOuts), block);
            juce::Random random (42);

            for (int pos = 0; pos < numSamples; pos += block)
            {
                const auto n = juce::jmin (block, numSamples - pos);

                for (int ch = 0; ch < numIns; ++ch)
                    for (int i = 0; i < n; ++i)
                        ins.setSample (ch, i, ch == status.config.inputChannel && input != nullptr
                                                  ? input (sampleCounter + i)
                                                  : random.nextFloat() * 0.01f);

                for (int ch = 0; ch < numOuts; ++ch)
                    juce::FloatVectorOperations::fill (outs.getWritePointer (ch), 0.5f, n);   // garbage

                if (callback != nullptr && running)
                    callback->process (ins.getArrayOfReadPointers(), numIns, outs.getArrayOfWritePointers(), numOuts, n);

                for (int ch = 0; ch < numOuts; ++ch)
                    result.copyFrom (ch, pos, outs, ch, 0, n);

                sampleCounter += n;
            }

            return result;
        }

        bool isRunning() const noexcept   { return running; }

    private:
        const Type* findType (const juce::String& name) const
        {
            for (const auto& t : types)
                if (t.name == name)
                    return &t;

            return nullptr;
        }

        const Device* findDevice (const juce::String& typeName, const juce::String& deviceName) const
        {
            if (const auto* t = findType (typeName))
                for (const auto& d : t->devices)
                    if (d.name == deviceName)
                        return &d;

            return nullptr;
        }

        template <typename T>
        static T nearest (const juce::Array<T>& values, T wanted)
        {
            auto best = values.getFirst();

            for (auto v : values)
                if (std::abs ((double) v - (double) wanted) < std::abs ((double) best - (double) wanted))
                    best = v;

            return best;
        }

        void startStream()
        {
            if (callback == nullptr || ! status.isOpen)
                return;

            engine::StreamLayout layout;
            layout.sampleRate = status.config.sampleRate;
            layout.bufferSize = status.config.bufferSize;
            layout.inputIndex = status.config.inputChannel;     // all channels are passed
            layout.outputIndex = status.config.outputChannel;
            callback->streamStarting (layout);
            running = true;
        }

        void stopStream()
        {
            if (running && callback != nullptr)
                callback->streamStopped();

            running = false;
        }

        engine::DeviceStatus status;
        engine::DuplexCallback* callback = nullptr;
        bool running = false;
        juce::int64 sampleCounter = 0;
    };

    /** A machine like the development Mac plus an Apollo-style interface. */
    inline void addStudioDevices (FakeAudioDevice& fake, bool withApollo)
    {
        FakeAudioDevice::Type coreAudio;
        coreAudio.name = "CoreAudio";

        FakeAudioDevice::Device mic;
        mic.name = "MacBook Pro Microphone";
        mic.inputs = { "Input 1" };

        FakeAudioDevice::Device speakers;
        speakers.name = "MacBook Pro Speakers";
        speakers.outputs = { "Output 1", "Output 2" };

        coreAudio.devices = { mic, speakers };

        if (withApollo)
        {
            FakeAudioDevice::Device apollo;
            apollo.name = "Apollo Twin";
            apollo.inputs = { "Mic/Line 1", "Mic/Line 2", "Hi-Z 1", "Virtual 1" };
            apollo.outputs = { "Monitor L", "Monitor R", "Line 3", "Line 4", "Headphone L" };
            apollo.bufferSizes = { 32, 64, 128, 256, 512, 1024 };
            coreAudio.devices.push_back (apollo);
        }

        coreAudio.defaultInput = mic.name;
        coreAudio.defaultOutput = speakers.name;
        fake.types.push_back (coreAudio);
    }
}
