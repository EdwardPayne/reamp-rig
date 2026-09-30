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
            else if (optionValue (arg, "view", value) && value.containsChar (':'))
                options.view = juce::Range<double> (value.upToFirstOccurrenceOf (":", false, false).getDoubleValue(),
                                                    value.fromFirstOccurrenceOf (":", false, false).getDoubleValue());
        }

        return options;
    }
}
