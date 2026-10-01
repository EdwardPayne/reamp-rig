#include "JuceAudioDevice.h"
#include "DeviceSession.h"

namespace rf::engine
{
    namespace
    {
        int firstSetBit (const juce::BigInteger& bits)
        {
            return bits.isZero() ? -1 : bits.findNextSetBit (0);
        }
    }

    JuceAudioDevice::JuceAudioDevice()
    {
        manager.getAvailableDeviceTypes();   // creates the types and scans for devices
        manager.addChangeListener (this);
        manager.addAudioCallback (this);
    }

    JuceAudioDevice::~JuceAudioDevice()
    {
        cancelPendingUpdate();
        manager.removeAudioCallback (this);
        manager.removeChangeListener (this);
        manager.closeAudioDevice();
    }

    //==============================================================================
    juce::AudioIODeviceType* JuceAudioDevice::findType (const juce::String& typeName)
    {
        for (auto* type : manager.getAvailableDeviceTypes())
            if (type->getTypeName() == typeName)
                return type;

        return nullptr;
    }

    juce::StringArray JuceAudioDevice::getTypeNames()
    {
        juce::StringArray names;

        for (auto* type : manager.getAvailableDeviceTypes())
            names.add (type->getTypeName());

        return names;
    }

    juce::StringArray JuceAudioDevice::getDeviceNames (const juce::String& typeName, bool inputs)
    {
        if (auto* type = findType (typeName))
            return type->getDeviceNames (inputs);

        return {};
    }

    juce::String JuceAudioDevice::getDefaultDeviceName (const juce::String& typeName, bool input)
    {
        if (auto* type = findType (typeName))
            return type->getDeviceNames (input)[type->getDefaultDeviceIndex (input)];

        return {};
    }

    bool JuceAudioDevice::hasSeparateInputsAndOutputs (const juce::String& typeName)
    {
        if (auto* type = findType (typeName))
            return type->hasSeparateInputsAndOutputs();

        return false;
    }

    //==============================================================================
    juce::String JuceAudioDevice::open (const DeviceConfig& config)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (findType (config.typeName) == nullptr)
            return "Driver \"" + config.typeName + "\" is not available";

        expected = config;

        // Switching driver type makes JUCE open that type's default devices; the expected-device
        // check in audioDeviceAboutToStart keeps the callback away from them. On macOS there is
        // only CoreAudio, so this never happens there.
        if (manager.getCurrentAudioDeviceType() != config.typeName)
            manager.setCurrentAudioDeviceType (config.typeName, false);

        // A direction without a channel is left out of the device entirely. For the input this
        // matters on macOS: creating a CoreAudio device that includes an input blocks inside
        // coreaudiod until the microphone permission prompt is answered.
        const auto inputName  = config.inputChannel  >= 0 ? config.inputDevice  : juce::String();
        const auto outputName = config.outputChannel >= 0 ? config.outputDevice : juce::String();

        if (inputName.isEmpty() && outputName.isEmpty())
            return "No channels to open";

        auto setup = manager.getAudioDeviceSetup();
        const auto sameDevices = manager.getCurrentAudioDevice() != nullptr
                              && setup.inputDeviceName == inputName
                              && setup.outputDeviceName == outputName;

        setup.inputDeviceName = inputName;
        setup.outputDeviceName = outputName;
        setup.sampleRate = config.sampleRate;
        setup.bufferSize = config.bufferSize;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;

        if (! sameDevices)
        {
            // Step 1: create the device without opening a stream, to learn its channels.
            setup.inputChannels.clear();
            setup.outputChannels.clear();

            if (auto error = manager.setAudioDeviceSetup (setup, false); error.isNotEmpty())
                return error;
        }

        auto* device = manager.getCurrentAudioDevice();

        if (device == nullptr)
            return "Could not create the audio device";

        const auto numIns  = device->getInputChannelNames().size();
        const auto numOuts = device->getOutputChannelNames().size();

        setup.inputChannels.clear();
        setup.outputChannels.clear();

        if (juce::isPositiveAndBelow (config.inputChannel, numIns))
            setup.inputChannels.setBit (config.inputChannel);

        if (juce::isPositiveAndBelow (config.outputChannel, numOuts))
            setup.outputChannels.setBit (config.outputChannel);

        if (setup.inputChannels.isZero() && setup.outputChannels.isZero())
            return "No channels to open";

        // Step 2: open the stream with exactly the selected channels.
        auto error = manager.setAudioDeviceSetup (setup, false);

        if (error.isEmpty() && (manager.getCurrentAudioDevice() == nullptr || ! manager.getCurrentAudioDevice()->isOpen()))
            error = "The audio device did not start";

        notifyListeners();
        return error;
    }

    void JuceAudioDevice::close()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        expected = {};
        manager.closeAudioDevice();
        notifyListeners();
    }

    DeviceStatus JuceAudioDevice::getStatus()
    {
        DeviceStatus status;

        {
            const std::scoped_lock lock (errorLock);
            status.lastError = asyncError;
        }

        auto* device = manager.getCurrentAudioDevice();

        if (device == nullptr)
            return status;

        const auto setup = manager.getAudioDeviceSetup();

        status.isOpen = device->isOpen() && isExpectedDevice (*device);
        status.config.typeName = device->getTypeName();
        status.config.inputDevice = setup.inputDeviceName;
        status.config.outputDevice = setup.outputDeviceName;
        status.config.sampleRate = device->getCurrentSampleRate();
        status.config.bufferSize = device->getCurrentBufferSizeSamples();
        status.inputChannelNames = device->getInputChannelNames();
        status.outputChannelNames = device->getOutputChannelNames();
        status.config.inputChannel = firstSetBit (device->getActiveInputChannels());
        status.config.outputChannel = firstSetBit (device->getActiveOutputChannels());
        status.config.inputChannelName = status.inputChannelNames[status.config.inputChannel];
        status.config.outputChannelName = status.outputChannelNames[status.config.outputChannel];
        status.sampleRates = device->getAvailableSampleRates();
        status.bufferSizes = device->getAvailableBufferSizes();

        if (status.isOpen)
        {
            status.inputLatencySamples = setup.inputDeviceName.isNotEmpty() ? device->getInputLatencyInSamples() : 0;
            status.outputLatencySamples = device->getOutputLatencyInSamples();
        }

        return status;
    }

    void JuceAudioDevice::setCallback (DuplexCallback* newCallback)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (newCallback == callback)
            return;

        // Removing takes the manager's callback lock, so no callback is running afterwards;
        // it also calls audioDeviceStopped (and adding calls audioDeviceAboutToStart).
        manager.removeAudioCallback (this);
        callback = newCallback;
        manager.addAudioCallback (this);
    }

    bool JuceAudioDevice::isExpectedDevice (juce::AudioIODevice& device) const
    {
        // JUCE names a device after its output, or its input when it has no output.
        const auto& name = expected.outputChannel >= 0 && expected.outputDevice.isNotEmpty() ? expected.outputDevice
                                                                                           : expected.inputDevice;
        return name.isNotEmpty() && device.getName() == name && device.getTypeName() == expected.typeName;
    }

    //==============================================================================
    void JuceAudioDevice::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        const auto ok = device != nullptr && isExpectedDevice (*device);
        forwarding.store (false);

        if (callbackStarted && callback != nullptr)
            callback->streamStopped();

        callbackStarted = false;

        if (ok && callback != nullptr)
        {
            StreamLayout layout;
            layout.sampleRate = device->getCurrentSampleRate();
            layout.bufferSize = device->getCurrentBufferSizeSamples();
            layout.inputIndex = packedChannelIndex (device->getActiveInputChannels(), expected.inputChannel);
            layout.outputIndex = packedChannelIndex (device->getActiveOutputChannels(), expected.outputChannel);

            callback->streamStarting (layout);
            callbackStarted = true;
            forwarding.store (true);
        }
    }

    void JuceAudioDevice::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                            float* const* outputs, int numOutputs, int numSamples,
                                                            const juce::AudioIODeviceCallbackContext&)
    {
        if (forwarding.load (std::memory_order_acquire) && callback != nullptr)
        {
            callback->process (inputs, numInputs, outputs, numOutputs, numSamples);
            return;
        }

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    }

    void JuceAudioDevice::audioDeviceStopped()
    {
        forwarding.store (false);

        if (callbackStarted && callback != nullptr)
            callback->streamStopped();

        callbackStarted = false;
    }

    void JuceAudioDevice::audioDeviceError (const juce::String& message)
    {
        // May arrive on a driver thread (never the real-time callback): hand it over.
        {
            const std::scoped_lock lock (errorLock);
            asyncError = message;
        }

        triggerAsyncUpdate();
    }

    void JuceAudioDevice::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        notifyListeners();
    }

    void JuceAudioDevice::handleAsyncUpdate()
    {
        notifyListeners();
    }
}
