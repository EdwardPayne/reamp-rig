#include "DeviceSession.h"

namespace rf::engine
{
    namespace
    {
        juce::String quoted (const juce::String& s)
        {
            return "\"" + s + "\"";
        }

        juce::String describeRate (double hz)
        {
            auto khz = juce::String (hz / 1000.0, 2);

            while (khz.containsChar ('.') && (khz.endsWithChar ('0') || khz.endsWithChar ('.')))
                khz = khz.dropLastCharacters (1);

            return khz + " kHz";
        }

        bool hasPreference (const juce::String& name, int index)
        {
            return name.isNotEmpty() || index >= 0;
        }

        juce::String describeWantedChannel (const juce::String& name, int index)
        {
            return name.isNotEmpty() ? quoted (name) : "channel " + juce::String (index + 1);
        }
    }

    //==============================================================================
    ChannelChoice resolveChannel (const juce::StringArray& names, const juce::String& wantedName, int wantedIndex)
    {
        if (names.isEmpty())
            return { -1, hasPreference (wantedName, wantedIndex) };

        if (wantedName.isNotEmpty())
        {
            if (const auto byName = names.indexOf (wantedName); byName >= 0)
                return { byName, false };

            // Name gone: keep the index if it still exists, but report the change.
            return { juce::isPositiveAndBelow (wantedIndex, names.size()) ? wantedIndex : 0, true };
        }

        if (juce::isPositiveAndBelow (wantedIndex, names.size()))
            return { wantedIndex, false };

        return { 0, wantedIndex >= 0 };
    }

    int packedChannelIndex (const juce::BigInteger& openChannels, int channel)
    {
        if (channel < 0 || ! openChannels[channel])
            return -1;

        return channel == 0 ? 0 : openChannels.getBitRange (0, channel).countNumberOfSetBits();
    }

    //==============================================================================
    DeviceConfig DeviceSession::resolveDevices (const DeviceConfig& wanted, juce::StringArray& warnings)
    {
        auto config = wanted;
        const auto types = device.getTypeNames();

        if (types.isEmpty())
        {
            config.typeName = {};
            config.inputDevice = config.outputDevice = {};
            return config;
        }

        if (! types.contains (wanted.typeName))
        {
            config.typeName = types[0];

            if (wanted.typeName.isNotEmpty())
                warnings.add ("Driver " + quoted (wanted.typeName) + " is not available; using " + quoted (config.typeName));
        }

        const auto outputs = device.getDeviceNames (config.typeName, false);
        const auto inputs  = device.getDeviceNames (config.typeName, true);

        auto pickDefault = [&] (const juce::StringArray& names, bool input) -> juce::String
        {
            const auto def = device.getDefaultDeviceName (config.typeName, input);
            return names.contains (def) ? def : names[0];
        };

        if (! outputs.contains (wanted.outputDevice))
        {
            config.outputDevice = pickDefault (outputs, false);

            if (wanted.outputDevice.isNotEmpty())
                warnings.add ("Output device " + quoted (wanted.outputDevice) + " not found; "
                              + (config.outputDevice.isNotEmpty() ? "using " + quoted (config.outputDevice)
                                                                  : juce::String ("no output device available")));
        }

        if (! device.hasSeparateInputsAndOutputs (config.typeName))
        {
            // One device for both directions (ASIO): the input follows the output.
            config.inputDevice = inputs.contains (config.outputDevice) ? config.outputDevice : juce::String();
        }
        else if (! inputs.contains (wanted.inputDevice))
        {
            // Prefer the output device itself when it has inputs: one duplex device is the
            // sample-synchronized setup the app is built for.
            config.inputDevice = inputs.contains (config.outputDevice) ? config.outputDevice
                                                                       : pickDefault (inputs, true);

            if (wanted.inputDevice.isNotEmpty())
                warnings.add ("Input device " + quoted (wanted.inputDevice) + " not found; "
                              + (config.inputDevice.isNotEmpty() ? "using " + quoted (config.inputDevice)
                                                                 : juce::String ("no input device available")));
        }

        return config;
    }

    DeviceSession::Result DeviceSession::open (const DeviceConfig& wanted, bool openInput, Mode mode)
    {
        Result result;
        const auto exact = mode == Mode::exact;
        auto config = resolveDevices (wanted, result.warnings);

        const auto wantedLabel = quoted (wanted.outputDevice.isNotEmpty() ? wanted.outputDevice : wanted.inputDevice);
        const auto wantedSetup = wanted.sampleRate > 0.0 ? " at " + describeRate (wanted.sampleRate) : juce::String();

        if (exact && (config.typeName != wanted.typeName || config.outputDevice != wanted.outputDevice
                      || config.inputDevice != wanted.inputDevice))
        {
            // Never another device: the one asked for is not listed. Nothing is touched.
            result.error = wantedLabel + " is not available, so it was not reopened" + wantedSetup;
            result.config = wanted;
            result.warnings.clear();
            return result;
        }

        if (config.typeName.isEmpty())
        {
            result.error = "No audio drivers are available";
            device.close();
            return result;
        }

        if (config.inputDevice.isEmpty() && config.outputDevice.isEmpty())
        {
            result.error = "No audio devices found";
            device.close();
            return result;
        }

        auto withInputRule = [openInput] (DeviceConfig c)
        {
            if (! openInput)
                c.inputChannel = -1;
            else if (c.inputChannel < 0 && c.inputDevice.isNotEmpty())
                c.inputChannel = 0;     // first guess; resolved by name below

            if (c.outputChannel < 0 && c.outputDevice.isNotEmpty())
                c.outputChannel = 0;

            return c;
        };

        // First attempt with the wanted channel indices: in the normal case (same device, same
        // channels) this is the only open.
        auto attempt = withInputRule (config);
        auto error = device.open (attempt);

        if (error.isNotEmpty() && exact)
        {
            device.close();
            result.error = wantedLabel + " could not be opened" + wantedSetup + " (" + error + ")";
            result.config = config;
            return result;
        }

        if (error.isNotEmpty())
        {
            // The device is present but will not open (busy, unsupported setup): try the
            // system defaults once before giving up.
            auto fallback = config;
            fallback.outputDevice = device.getDefaultDeviceName (config.typeName, false);
            fallback.inputDevice  = device.getDefaultDeviceName (config.typeName, true);
            fallback.sampleRate = 0.0;
            fallback.bufferSize = 0;

            if (! device.hasSeparateInputsAndOutputs (config.typeName))
                fallback.inputDevice = fallback.outputDevice;

            const auto differs = fallback.outputDevice != config.outputDevice || fallback.inputDevice != config.inputDevice;

            if (differs && (fallback.outputDevice.isNotEmpty() || fallback.inputDevice.isNotEmpty())
                && device.open (withInputRule (fallback)).isEmpty())
            {
                result.warnings.add ("Could not open " + quoted (config.outputDevice.isNotEmpty() ? config.outputDevice
                                                                                                   : config.inputDevice)
                                     + " (" + error + "); using the system default devices");
                config = fallback;
                attempt = withInputRule (fallback);
                error = {};
            }
        }

        if (error.isNotEmpty())
        {
            device.close();
            result.error = error;
            result.config = config;
            return result;
        }

        const auto status = device.getStatus();
        const auto deviceLabel = quoted (config.outputDevice.isNotEmpty() ? config.outputDevice : config.inputDevice);

        if (wanted.sampleRate > 0.0 && ! juce::approximatelyEqual (status.config.sampleRate, wanted.sampleRate)
            && config.outputDevice == wanted.outputDevice)
            result.warnings.add (describeRate (wanted.sampleRate) + " is not available on " + deviceLabel
                                 + "; using " + describeRate (status.config.sampleRate));

        if (wanted.bufferSize > 0 && status.config.bufferSize != wanted.bufferSize
            && config.outputDevice == wanted.outputDevice)
            result.warnings.add ("Buffer size " + juce::String (wanted.bufferSize) + " is not available on " + deviceLabel
                                 + "; using " + juce::String (status.config.bufferSize));

        if (exact && ((wanted.sampleRate > 0.0 && ! juce::approximatelyEqual (status.config.sampleRate, wanted.sampleRate))
                      || (wanted.bufferSize > 0 && status.config.bufferSize != wanted.bufferSize)))
        {
            // It opened, but not as asked (the driver picked the nearest rate or buffer size).
            const auto rateOff = wanted.sampleRate > 0.0 && ! juce::approximatelyEqual (status.config.sampleRate, wanted.sampleRate);
            result.error = wantedLabel + " runs at "
                         + (rateOff ? describeRate (status.config.sampleRate) + " instead of " + describeRate (wanted.sampleRate)
                                    : "buffer " + juce::String (status.config.bufferSize) + " instead of "
                                      + juce::String (wanted.bufferSize));
            result.warnings.clear();
            config.sampleRate = status.config.sampleRate;
            config.bufferSize = status.config.bufferSize;
            result.config = config;
            return result;
        }

        config.sampleRate = status.config.sampleRate;
        config.bufferSize = status.config.bufferSize;

        // Channels: by name, then index, else the first one.
        auto applyChannel = [&] (const juce::StringArray& names, const juce::String& wantedName, int wantedIndex,
                                 int& index, juce::String& name, const char* direction)
        {
            const auto choice = resolveChannel (names, wantedName, wantedIndex);
            index = choice.index;
            name = names[index];

            if (choice.changed && hasPreference (wantedName, wantedIndex))
                result.warnings.add (juce::String (direction) + " " + describeWantedChannel (wantedName, wantedIndex)
                                     + " not found; using " + (index >= 0 ? quoted (name) : juce::String ("none")));
        };

        applyChannel (status.outputChannelNames, wanted.outputChannelName, wanted.outputChannel,
                      config.outputChannel, config.outputChannelName, "Output channel");

        if (config.inputDevice.isNotEmpty() && openInput)
        {
            applyChannel (status.inputChannelNames, wanted.inputChannelName, wanted.inputChannel,
                          config.inputChannel, config.inputChannelName, "Input channel");
        }
        else if (config.inputDevice.isNotEmpty())
        {
            // Not opened: keep the wanted choice as it is (its names are unknown).
            config.inputChannel = wanted.inputDevice == config.inputDevice ? wanted.inputChannel : -1;
            config.inputChannelName = wanted.inputDevice == config.inputDevice ? wanted.inputChannelName : juce::String();
        }
        else
        {
            config.inputChannel = -1;
            config.inputChannelName = {};
        }

        auto final = withInputRule (config);

        if (final.inputChannel != attempt.inputChannel || final.outputChannel != attempt.outputChannel)
        {
            // Same devices, rate and buffer: only the open channels change.
            if (const auto reopenError = device.open (final); reopenError.isNotEmpty())
            {
                device.close();
                result.error = reopenError;
                result.config = config;
                return result;
            }
        }

        result.ok = true;
        result.config = config;
        result.inputOpen = final.inputChannel >= 0;
        return result;
    }

    bool DeviceSession::isPresent (const DeviceConfig& wanted)
    {
        if (wanted.outputDevice.isEmpty() && wanted.inputDevice.isEmpty())
            return false;

        if (! device.getTypeNames().contains (wanted.typeName))
            return false;

        if (wanted.outputDevice.isNotEmpty() && ! device.getDeviceNames (wanted.typeName, false).contains (wanted.outputDevice))
            return false;

        if (wanted.inputDevice.isNotEmpty() && ! device.getDeviceNames (wanted.typeName, true).contains (wanted.inputDevice))
            return false;

        return true;
    }

    bool DeviceSession::runs (const DeviceStatus& status, const DeviceConfig& wanted)
    {
        if (! status.isOpen || status.config.typeName != wanted.typeName)
            return false;

        if (wanted.outputDevice.isNotEmpty() && status.config.outputDevice != wanted.outputDevice)
            return false;

        if (wanted.inputDevice.isNotEmpty() && status.config.inputDevice.isNotEmpty() && status.config.inputDevice != wanted.inputDevice)
            return false;

        return true;
    }
}
