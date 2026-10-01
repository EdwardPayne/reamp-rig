#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

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
        --virtual-device           use a software device instead of the audio hardware
                                   (engine::LoopbackTestDevice, paced in real time): one
                                   driver "Virtual", one device "Virtual Interface" with 2 inputs
                                   and 4 outputs; inputs are silent, nothing reaches a speaker
        --virtual-loopback[=<n>]   implies --virtual-device; the selected output is fed back to
                                   the selected input one buffer plus <n> samples later
                                   (default 300); the device reports that round trip as its
                                   driver latency, so the "not calibrated" estimate is exact
        --virtual-rates=<Hz,...>   sample rates the virtual device offers (default 44100,48000,
                                   96000); e.g. 48000 to make every other file "resampled"
        --virtual-speed=<x>        run the virtual device <x> times faster than real time

        --audition-check[=<sec>]   once the lead file is loaded, audition it for <sec> seconds
                                   (default 2), print progress (playhead, meter peaks) to
                                   stderr, stop and quit with exit code 0 if playback advanced
                                   and the output level matched the file peak plus gain.
                                   With --snapshot, the snapshot is taken during playback instead.

        --batch-check=<folder>     once the --open scans finish, run a real batch of every queued
                                   file into <folder> (single output folder, mirrored structure;
                                   the other naming options as saved), print one line per file,
                                   then verify every output (length = source + tail, content =
                                   the played source channel times the output level: bit-exact,
                                   or within -80 dB when resampled) and quit with exit code 0 if
                                   all passed. Always runs on the virtual loopback device
                                   (--virtual-loopback=300 unless given) and never on hardware,
                                   unless --batch-check-hardware is added (then the content
                                   check is skipped: a real amp or room is in the loop).
                                   With --snapshot, the snapshot is taken mid-batch instead
                                   (a file in the middle of the list about half recorded).
        --batch-check-hardware     allow --batch-check on the selected real device
        --batch-check-transport    with --batch-check: pause file 2 at 30 % and resume it after a
                                   second, skip file 3, stop during file 5; passes if files 1, 2
                                   and 4 are Done and exact, 3 Skipped, 5 Queued, no temp files
        --sidebar-scroll=<name>    scroll the sidebar to a section (audio, sync, destination,
                                   options), e.g. to show DESTINATION in a snapshot

        Sync (phase 5):
        --sync-check               once the device is open (and the --open scans finished), run
                                   Sync on the current configuration, print every repeat and the
                                   result to stderr, store a passing measurement like the Sync
                                   button does, and quit with exit code 0 if it passed (on the
                                   virtual loopback: and equals the loop's true round trip).
                                   Runs on the virtual loopback (--virtual-loopback=300 unless
                                   given) unless --sync-check-hardware is added. Combined with
                                   --batch-check, the batch check runs after it in the same
                                   process. With --snapshot, the snapshot waits for both.
        --sync-check-hardware      allow --sync-check on the selected real device
        --sync-check-rates=<Hz,...> also measure these sample rates (the device is reopened at
                                   each, as the batch does, and restored afterwards)
        --sync-level=<dBFS>        sync test signal level for this run (-60..0, not saved)
        --virtual-reported-latency=<n>  the virtual device reports a driver latency of n samples
                                   (in n/2 + out the rest) instead of its true round trip: a
                                   deliberately wrong estimate
        --settings-file=<path>     use this settings file instead of the user's (isolates checks:
                                   sync measurements made by --sync-check land there)
        --press-start              press Start once the --open scans finished (shows the "Not
                                   synced" confirmation when a configuration lacks a measurement;
                                   for snapshots)

        Phase 6:
        --window-bounds=<x>,<y>,<w>,<h>  move and resize the window after launch as if the user
                                   had dragged it (screen coordinates of the content, below the
                                   title bar); it is then remembered like any user change
        --press-forget             open the "Forget" confirmation of the SYNC section once the
                                   device is open (needs a measurement for the configuration;
                                   for snapshots)
        --virtual-unplug=<at>:<for>  the virtual interface is unplugged <at> seconds after launch
                                   and plugged back in <for> seconds later (checks that a batch
                                   pauses, the device reopens by itself, and --batch-check then
                                   resumes and passes)
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
        std::optional<int> virtualLoopbackDelay;
        juce::Array<double> virtualRates;
        double virtualSpeed = 1.0;
        std::optional<double> auditionCheckSeconds;
        std::optional<juce::File> batchCheckFolder;
        bool batchCheckHardware = false;
        bool batchCheckTransport = false;
        juce::String sidebarScroll;

        bool syncCheck = false;
        bool syncCheckHardware = false;
        juce::Array<double> syncCheckRates;
        std::optional<float> syncLevelDb;
        std::optional<int> virtualReportedLatency;
        std::optional<juce::File> settingsFile;
        bool pressStart = false;
        std::optional<juce::Rectangle<int>> windowBounds;
        bool pressForget = false;
        std::optional<std::pair<double, double>> virtualUnplug;

        static LaunchOptions parse (const juce::StringArray& args, const juce::File& workingDirectory);
    };
}
