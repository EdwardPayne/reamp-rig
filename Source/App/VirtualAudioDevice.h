#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Engine/AudioDeviceInterface.h"

namespace rf::app
{
    /*  Development aid (--virtual-device): a silent software device with its own paced audio
        thread, so the whole app path (device selection, audition, meters, playhead) can be
        exercised without hardware, without playing anything through speakers and without a
        macOS microphone prompt. Outputs are discarded; inputs are silent. It is not the
        phase 4 LoopbackTestDevice (no output-to-input loop, no delay model).

        One driver type "Virtual", one device "Virtual Interface" with 2 inputs and 4 outputs,
        44.1/48/96 kHz, buffers 64..1024 (default 48 kHz, 256).
    */
    class VirtualAudioDevice final : public engine::AudioDeviceInterface,
                                     private juce::Thread
    {
    public:
        VirtualAudioDevice();
        ~VirtualAudioDevice() override;

        juce::StringArray getTypeNames() override;
        juce::StringArray getDeviceNames (const juce::String& typeName, bool inputs) override;
        juce::String getDefaultDeviceName (const juce::String& typeName, bool input) override;
        bool hasSeparateInputsAndOutputs (const juce::String&) override   { return false; }

        juce::String open (const engine::DeviceConfig&) override;
        void close() override;
        engine::DeviceStatus getStatus() override                        { return status; }
        void setCallback (engine::DuplexCallback*) override;

    private:
        void run() override;
        void startStream();
        void stopStream();

        engine::DeviceStatus status;
        engine::DuplexCallback* callback = nullptr;
        bool streaming = false;

        juce::AudioBuffer<float> inputs, outputs;   // sized in open(), never on the audio thread

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualAudioDevice)
    };
}
