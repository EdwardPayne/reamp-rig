#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioDeviceInterface.h"

#include <mutex>

namespace rf::engine
{
    /*  AudioDeviceInterface over juce::AudioDeviceManager.

        - Only the selected input channel and the selected output channel are opened, so the
          driver streams exactly what the engine uses; CoreAudio leaves unopened outputs silent.
          The callback is told where those channels are in the (packed) buffers.
        - On macOS, a separate input and output device are combined by JUCE into one private
          aggregate device, so there is still a single duplex callback; the pair is not
          sample-synchronized (drift-corrected clocks), which the UI labels as test-only.
        - The manager's own XML state and "default device on failure" logic are not used: a
          device is only ever opened by an explicit open(). If the manager ever starts a
          device other than the one requested (for example after a device list change), the
          callback is not forwarded to it and the outputs are zeroed.
        - Change notifications from the manager (device list changed, device started/stopped)
          are forwarded to listeners on the message thread.
    */
    class JuceAudioDevice final : public AudioDeviceInterface,
                                  private juce::AudioIODeviceCallback,
                                  private juce::ChangeListener,
                                  private juce::AsyncUpdater
    {
    public:
        JuceAudioDevice();
        ~JuceAudioDevice() override;

        juce::StringArray getTypeNames() override;
        juce::StringArray getDeviceNames (const juce::String& typeName, bool inputs) override;
        juce::String getDefaultDeviceName (const juce::String& typeName, bool input) override;
        bool hasSeparateInputsAndOutputs (const juce::String& typeName) override;

        juce::String open (const DeviceConfig&) override;
        void close() override;
        DeviceStatus getStatus() override;
        void setCallback (DuplexCallback*) override;

    private:
        juce::AudioIODeviceType* findType (const juce::String&);
        bool isExpectedDevice (juce::AudioIODevice&) const;

        // juce::AudioIODeviceCallback
        void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                               float* const* outputs, int numOutputs, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;
        void audioDeviceError (const juce::String& message) override;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void handleAsyncUpdate() override;

        juce::AudioDeviceManager manager;

        DuplexCallback* callback = nullptr;     // changed only while detached from the manager
        bool callbackStarted = false;           // message thread
        std::atomic<bool> forwarding { false };

        DeviceConfig expected;                  // what open() asked for (message thread)

        std::mutex errorLock;                   // never taken on the audio thread
        juce::String asyncError;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceAudioDevice)
    };
}
