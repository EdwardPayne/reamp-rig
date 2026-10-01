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

        Device selection for this run only (not saved; any change made in the UI afterwards is
        saved as usual). Used to test with a specific device without touching the settings:
        --device-type=<name>       driver type, e.g. CoreAudio
        --device=<name>            input and output device (one duplex device)
        --output-device=<name>     output device only
        --input-device=<name>      input device only
        --sample-rate=<Hz>         e.g. 48000
        --buffer-size=<samples>    e.g. 256
        --output-channel=<n|name>  1-based channel number or the driver's channel name
        --input-channel=<n|name>
        --output-level=<dB>        output level, -60..+12
        --no-input                 open the output only and do not ask for microphone access
                                   (only helps with output-only devices: on macOS a device
                                   that has inputs cannot be opened before the permission
                                   prompt has been answered)
        --virtual-device           use a silent software device instead of the audio hardware
                                   (App/VirtualAudioDevice.h); nothing is played or recorded

        --audition-check[=<sec>]   once the lead file is loaded, audition it for <sec> seconds
                                   (default 2), print progress (playhead, meter peaks) to
                                   stderr, stop and quit with exit code 0 if playback advanced
                                   and the output level matched the file peak plus gain.
                                   With --snapshot, the snapshot is taken during playback instead.
    */
    struct LaunchOptions
    {
        juce::Array<juce::File> openPaths;
        std::optional<juce::File> snapshotFile;
        juce::StringArray selectNames;
        std::optional<double> auditionStart;
        std::optional<juce::Range<double>> view;

        std::optional<juce::String> deviceType, inputDevice, outputDevice;
        std::optional<double> sampleRate;
        std::optional<int> bufferSize;
        std::optional<juce::String> outputChannel, inputChannel;
        std::optional<float> outputLevelDb;
        bool noInput = false;
        bool virtualDevice = false;
        std::optional<double> auditionCheckSeconds;

        static LaunchOptions parse (const juce::StringArray& args, const juce::File& workingDirectory);
    };
}
