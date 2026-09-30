#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace rf::app
{
    /*  Command-line options. Relative paths resolve against the current working directory.

        --open=<path>          (repeatable) add a file or folder on startup, as if dropped;
                               uses the persisted "Include subfolders" setting.

        Development aids, used with --snapshot to verify the UI without a mouse:
        --snapshot=<file.png>  render the window to a PNG once loading has finished, then quit.
        --select=<file name>   (repeatable) select these rows after the --open scans finish;
                               the first one becomes the lead row shown in the waveform panel.
        --audition-at=<sec>    place the audition start marker.
        --view=<start>:<end>   zoom the waveform to this time range (seconds).
    */
    struct LaunchOptions
    {
        juce::Array<juce::File> openPaths;
        std::optional<juce::File> snapshotFile;
        juce::StringArray selectNames;
        std::optional<double> auditionStart;
        std::optional<juce::Range<double>> view;

        static LaunchOptions parse (const juce::StringArray& args, const juce::File& workingDirectory);
    };
}
