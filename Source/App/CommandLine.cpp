#include "CommandLine.h"

namespace rf::app
{
    namespace
    {
        bool optionValue (const juce::String& arg, const char* name, juce::String& value)
        {
            const auto prefix = juce::String ("--") + name + "=";

            if (! arg.startsWith (prefix))
                return false;

            value = arg.fromFirstOccurrenceOf ("=", false, false).unquoted();
            return true;
        }
    }

    namespace
    {
        juce::File resolve (const juce::File& cwd, const juce::String& path)
        {
            // juce::File expands a leading "~" for absolute paths; getChildFile handles the rest.
            return path.startsWith ("~") ? juce::File (path) : cwd.getChildFile (path);
        }
    }

    LaunchOptions LaunchOptions::parse (const juce::StringArray& args, const juce::File& cwd)
    {
        LaunchOptions options;
        juce::String value;

        for (const auto& arg : args)
        {
            if (optionValue (arg, "open", value) && value.isNotEmpty())
                options.openPaths.add (resolve (cwd, value));
            else if (optionValue (arg, "snapshot", value) && value.isNotEmpty())
                options.snapshotFile = resolve (cwd, value);
            else if (optionValue (arg, "select", value) && value.isNotEmpty())
                options.selectNames.add (value);
            else if (optionValue (arg, "audition-at", value))
                options.auditionStart = value.getDoubleValue();
            else if (optionValue (arg, "device-type", value))
                options.deviceType = value;
            else if (optionValue (arg, "device", value))
                options.inputDevice = options.outputDevice = value;
            else if (optionValue (arg, "output-device", value))
                options.outputDevice = value;
            else if (optionValue (arg, "input-device", value))
                options.inputDevice = value;
            else if (optionValue (arg, "sample-rate", value))
                options.sampleRate = value.getDoubleValue();
            else if (optionValue (arg, "buffer-size", value))
                options.bufferSize = value.getIntValue();
            else if (optionValue (arg, "output-channel", value))
                options.outputChannel = value;
            else if (optionValue (arg, "input-channel", value))
                options.inputChannel = value;
            else if (optionValue (arg, "output-level", value))
                options.outputLevelDb = (float) value.getDoubleValue();
            else if (arg == "--no-input")
                options.noInput = true;
            else if (arg == "--virtual-device")
                options.virtualDevice = true;
            else if (arg == "--virtual-loopback")
                options.virtualLoopbackDelay = 300;
            else if (optionValue (arg, "virtual-loopback", value))
                options.virtualLoopbackDelay = juce::jmax (0, value.getIntValue());
            else if (optionValue (arg, "virtual-rates", value))
            {
                for (const auto& r : juce::StringArray::fromTokens (value, ",", {}))
                    if (r.getDoubleValue() > 0.0)
                        options.virtualRates.add (r.getDoubleValue());
            }
            else if (optionValue (arg, "virtual-speed", value))
                options.virtualSpeed = juce::jlimit (0.1, 100.0, value.getDoubleValue());
            else if (optionValue (arg, "batch-check", value) && value.isNotEmpty())
                options.batchCheckFolder = resolve (cwd, value);
            else if (arg == "--batch-check-hardware")
                options.batchCheckHardware = true;
            else if (arg == "--batch-check-transport")
                options.batchCheckTransport = true;
            else if (optionValue (arg, "sidebar-scroll", value))
                options.sidebarScroll = value;
            else if (arg == "--audition-check")
                options.auditionCheckSeconds = 2.0;
            else if (optionValue (arg, "audition-check", value))
                options.auditionCheckSeconds = juce::jmax (0.1, value.getDoubleValue());
            else if (optionValue (arg, "view", value) && value.containsChar (':'))
                options.view = juce::Range<double> (value.upToFirstOccurrenceOf (":", false, false).getDoubleValue(),
                                                    value.fromFirstOccurrenceOf (":", false, false).getDoubleValue());
        }

        // A batch check never touches the audio hardware unless explicitly allowed.
        if (options.batchCheckFolder.has_value() && ! options.batchCheckHardware && ! options.virtualLoopbackDelay.has_value())
            options.virtualLoopbackDelay = 300;

        if (options.virtualLoopbackDelay.has_value())
            options.virtualDevice = true;

        return options;
    }
}
