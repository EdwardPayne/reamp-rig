#include "VirtualAudioDevice.h"

namespace rf::app
{
    namespace
    {
        const juce::String typeName ("Virtual");
        const juce::String deviceName ("Virtual Interface");
        const juce::StringArray inputNames { "Virtual In 1", "Virtual In 2" };
        const juce::StringArray outputNames { "Virtual Out 1", "Virtual Out 2", "Virtual Out 3", "Virtual Out 4" };
        const juce::Array<double> rates { 44100.0, 48000.0, 96000.0 };
        const juce::Array<int> bufferSizes { 64, 128, 256, 512, 1024 };

        template <typename T>
        T nearest (const juce::Array<T>& values, T wanted)
        {
            auto best = values.getFirst();

            for (auto v : values)
                if (std::abs ((double) v - (double) wanted) < std::abs ((double) best - (double) wanted))
                    best = v;

            return best;
        }
    }

    VirtualAudioDevice::VirtualAudioDevice()
        : juce::Thread ("Virtual audio device")
    {
    }

    VirtualAudioDevice::~VirtualAudioDevice()
    {
        stopStream();
    }

    juce::StringArray VirtualAudioDevice::getTypeNames()
    {
        return { typeName };
    }

    juce::StringArray VirtualAudioDevice::getDeviceNames (const juce::String& type, bool)
    {
        return type == typeName ? juce::StringArray (deviceName) : juce::StringArray();
    }

    juce::String VirtualAudioDevice::getDefaultDeviceName (const juce::String& type, bool)
    {
        return type == typeName ? deviceName : juce::String();
    }

    juce::String VirtualAudioDevice::open (const engine::DeviceConfig& config)
    {
        stopStream();

        if (config.typeName != typeName || (config.outputDevice != deviceName && config.inputDevice != deviceName))
            return "No such device";

        status = {};
        status.config = config;
        status.config.inputDevice = status.config.outputDevice = deviceName;
        status.config.sampleRate = nearest (rates, config.sampleRate > 0.0 ? config.sampleRate : 48000.0);
        status.config.bufferSize = nearest (bufferSizes, config.bufferSize > 0 ? config.bufferSize : 256);

        if (! juce::isPositiveAndBelow (config.inputChannel, inputNames.size()))
            status.config.inputChannel = -1;

        if (! juce::isPositiveAndBelow (config.outputChannel, outputNames.size()))
            status.config.outputChannel = -1;

        status.inputChannelNames = inputNames;
        status.outputChannelNames = outputNames;
        status.config.inputChannelName = inputNames[status.config.inputChannel];
        status.config.outputChannelName = outputNames[status.config.outputChannel];
        status.sampleRates = rates;
        status.bufferSizes = bufferSizes;
        status.isOpen = true;

        inputs.setSize (inputNames.size(), status.config.bufferSize);
        inputs.clear();
        outputs.setSize (outputNames.size(), status.config.bufferSize);

        startStream();
        notifyListeners();
        return {};
    }

    void VirtualAudioDevice::close()
    {
        stopStream();
        status = {};
        notifyListeners();
    }

    void VirtualAudioDevice::setCallback (engine::DuplexCallback* newCallback)
    {
        stopStream();
        callback = newCallback;
        startStream();
    }

    void VirtualAudioDevice::startStream()
    {
        if (callback == nullptr || ! status.isOpen)
            return;

        engine::StreamLayout layout;
        layout.sampleRate = status.config.sampleRate;
        layout.bufferSize = status.config.bufferSize;
        layout.inputIndex = status.config.inputChannel;     // all channels are passed
        layout.outputIndex = status.config.outputChannel;

        callback->streamStarting (layout);
        streaming = true;
        startThread (juce::Thread::Priority::highest);
    }

    void VirtualAudioDevice::stopStream()
    {
        stopThread (2000);

        if (streaming && callback != nullptr)
            callback->streamStopped();

        streaming = false;
    }

    void VirtualAudioDevice::run()
    {
        // Paced like a real device: one block every bufferSize / sampleRate seconds.
        const auto blockMs = 1000.0 * status.config.bufferSize / status.config.sampleRate;
        auto next = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            callback->process (inputs.getArrayOfReadPointers(), inputs.getNumChannels(),
                               outputs.getArrayOfWritePointers(), outputs.getNumChannels(),
                               status.config.bufferSize);

            next += blockMs;
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();

            if (wait > 0.0)
                juce::Thread::wait (juce::jmax (1, (int) wait));
            else
                next = juce::Time::getMillisecondCounterHiRes();   // fell behind: do not burst
        }
    }
}
