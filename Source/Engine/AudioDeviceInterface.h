#pragma once

#include <juce_core/juce_core.h>

namespace rf::engine
{
    /*  Abstract audio device (PROMPT.md section 4.5).

        Everything the engine and the app need from an audio interface, kept small so that a
        simulated device (phase 4's LoopbackTestDevice, the fake device in Tests/) can
        implement it without hardware. The real implementation is JuceAudioDevice.

        Threading: every method is message-thread only, except DuplexCallback::process(),
        which the device calls on its audio thread. Listeners are called on the message thread.
    */

    //==============================================================================
    /** A device configuration: what to open. Channels are indices into the driver's channel
        name lists; -1 means "none" for that direction. The channel names are kept next to
        the indices so a saved choice can be found again if the driver reorders channels. */
    struct DeviceConfig
    {
        juce::String typeName;          // "CoreAudio", "ASIO", "Windows Audio", ...
        juce::String inputDevice;       // same as outputDevice for a single duplex device
        juce::String outputDevice;
        double sampleRate = 0.0;        // 0 = the device's current/default rate
        int bufferSize = 0;             // 0 = the device's default buffer size
        int inputChannel = -1;
        int outputChannel = -1;
        juce::String inputChannelName;
        juce::String outputChannelName;

        /** True when input and output are two different devices (macOS test setup with the
            built-in mic and speakers): not sample-synchronized, for testing only. */
        bool isSplit() const noexcept
        {
            return inputDevice.isNotEmpty() && outputDevice.isNotEmpty() && inputDevice != outputDevice;
        }

        bool operator== (const DeviceConfig& o) const
        {
            return typeName == o.typeName && inputDevice == o.inputDevice && outputDevice == o.outputDevice
                && juce::exactlyEqual (sampleRate, o.sampleRate) && bufferSize == o.bufferSize
                && inputChannel == o.inputChannel && outputChannel == o.outputChannel
                && inputChannelName == o.inputChannelName && outputChannelName == o.outputChannelName;
        }

        bool operator!= (const DeviceConfig& o) const   { return ! operator== (o); }
    };

    /** What the device is doing right now, for the GUI and the engine (a copy, never a view
        into device internals). */
    struct DeviceStatus
    {
        bool isOpen = false;
        DeviceConfig config;                    // what is actually open (rate/buffer as running)
        juce::StringArray inputChannelNames;    // all channels of the open input device
        juce::StringArray outputChannelNames;   // all channels of the open output device
        juce::Array<double> sampleRates;        // supported by the open device(s)
        juce::Array<int> bufferSizes;
        int inputLatencySamples = 0;            // driver-reported
        int outputLatencySamples = 0;
        juce::String lastError;
    };

    /** Where the selected channels are in the buffers handed to DuplexCallback::process. */
    struct StreamLayout
    {
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputIndex = -1;    // index into the input array, -1 if no input channel is open
        int outputIndex = -1;   // index into the output array, -1 if no output channel is open
    };

    /** The one duplex callback: input and output for the same instant in one call. */
    class DuplexCallback
    {
    public:
        virtual ~DuplexCallback() = default;

        /** Called before the first process() of a stream (message thread). */
        virtual void streamStarting (const StreamLayout&) = 0;

        /** Audio thread. Must fill every output channel (zeros for silence) and must not
            allocate, lock, log or touch files. Pointers in the arrays may be null. */
        virtual void process (const float* const* inputs, int numInputs,
                              float* const* outputs, int numOutputs, int numSamples) noexcept = 0;

        /** Called after the last process() of a stream (message thread). */
        virtual void streamStopped() = 0;
    };

    //==============================================================================
    class AudioDeviceInterface
    {
    public:
        virtual ~AudioDeviceInterface() = default;

        /** Device types (drivers) available on this machine. */
        virtual juce::StringArray getTypeNames() = 0;

        /** Devices of a type that have inputs (`inputs` true) or outputs. */
        virtual juce::StringArray getDeviceNames (const juce::String& typeName, bool inputs) = 0;

        /** The system default input/output device of a type, or empty. */
        virtual juce::String getDefaultDeviceName (const juce::String& typeName, bool input) = 0;

        /** False for drivers where input and output are always the same device (ASIO). */
        virtual bool hasSeparateInputsAndOutputs (const juce::String& typeName) = 0;

        /** Opens (or reconfigures) the device and starts streaming to the callback, if one is
            set. Channel indices outside the device's channel lists are not opened. A direction
            whose channel is -1 is not opened at all (its device is left out, so no channel
            names are reported for it). Returns an error message, or an empty string on success. */
        virtual juce::String open (const DeviceConfig&) = 0;
        virtual void close() = 0;

        virtual DeviceStatus getStatus() = 0;

        /** Sets the callback that receives the stream (nullptr detaches it). When this
            returns, the previous callback is guaranteed not to be running. */
        virtual void setCallback (DuplexCallback*) = 0;

        //==============================================================================
        class Listener
        {
        public:
            virtual ~Listener() = default;

            /** The device list or the open device's state changed (message thread). */
            virtual void audioDeviceChanged() = 0;
        };

        void addListener (Listener* l)      { listeners.add (l); }
        void removeListener (Listener* l)   { listeners.remove (l); }

    protected:
        void notifyListeners()              { listeners.call ([] (Listener& l) { l.audioDeviceChanged(); }); }

    private:
        juce::ListenerList<Listener> listeners;
    };
}
