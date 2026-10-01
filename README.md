# Reamp Forge

Batch re-amping of guitar DI tracks through a hardware amp via an audio interface.
Desktop app, C++20 + JUCE 9.0.3 (fetched automatically), CMake.

Status: **phase 4 (engine + batch)**. Files and folders can be dropped or added, are scanned
and listed grouped by folder with multi-select and L/R choice, and the selected file's waveform
is shown with zoom and an audition start marker. The audio device, sample rate, buffer size and
the output/input channels (with the driver's channel names) are chosen in the AUDIO section and
remembered; input/output meters, the output level and Audition (Space) work. **Start** records
every queued file through the amp: each file is played out of the output channel and the input
is recorded in the same audio callback, latency-compensated to exactly the source's length (plus
an optional tail), and written as WAV next to the source or into one folder. Latency is still
the driver's estimate ("not calibrated"); measuring it (Sync) comes in phase 5.

## Requirements

- macOS 11+ on Apple Silicon or Intel (Windows is planned, untested)
- CMake 3.22 or newer
- Xcode Command Line Tools (Apple Clang). Full Xcode is not needed.
- Network access on the first configure (CMake downloads JUCE 9.0.3, about 25 MB)

## Build

From the repository root:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
```

The app bundle is written to:

```
build/ReampForge_artefacts/Release/Reamp Forge.app
```

It is ad-hoc code signed as a post-build step, which is enough for local runs.

A clean Release build takes about 1 minute on an Apple Silicon Mac (configure about 20 s,
most of which is downloading JUCE and building its `juceaide` helper).

### Note on broken Command Line Tools installs

If `/Library/Developer/CommandLineTools/usr/include/c++/v1` exists but contains only a few
leftover folders (no `algorithm` header), Apple Clang picks it up instead of the SDK's libc++
headers and even `#include <algorithm>` fails. `CMakeLists.txt` detects this and adds
`-nostdinc++ -isystem <SDK>/usr/include/c++/v1` automatically, printing a CMake warning.
The permanent fix is to remove that stale directory or reinstall the Command Line Tools:

```sh
sudo rm -rf /Library/Developer/CommandLineTools/usr/include/c++
```

## Run

```sh
open "build/ReampForge_artefacts/Release/Reamp Forge.app"
```

or, to see stdout/stderr in the terminal:

```sh
"build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge"
```

To add files or folders at startup (repeatable; folders follow the "Include subfolders" setting):

```sh
"build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge" --open="$HOME/DI/Session A" --open=take.wav
```

## Tests

JUCE `UnitTest` cases in `Tests/`, built as `ReampForgeTests` with the app and run through ctest:

```sh
ctest --test-dir build --output-on-failure
```

### UI snapshot (development aid)

The app can render its own window (including open popup menus and tooltips) to a PNG and quit.
This works without granting the terminal screen-recording permission:

```sh
"build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge" --snapshot="$PWD/docs/phase1.png"
```

The native macOS title bar is not part of the snapshot. It waits until scans and the waveform
have finished. Combine it with `--open=` and the development flags `--select=<file name>`
(repeatable, first one is shown in the waveform panel), `--audition-at=<seconds>` and
`--view=<start>:<end>` to capture a populated window (see `Source/App/CommandLine.h`).

## Batch processing

1. Add files, pick the L/R channel of stereo files, set the AUDIO section (one interface for
   real work) and the output level (watch "Peak at output").
2. DESTINATION: **Subfolder next to source** (default, `<source folder>/Reamped/`) or **Single
   output folder** (choose it; "Mirror folder structure" recreates the folders below the folder
   you added, e.g. `<output>/Session A/Takes/`). Prefix and suffix make
   `<prefix><name><suffix>.wav` (example line underneath); OPTIONS "Append channel tag" adds
   `_L`/`_R` for stereo sources. Format WAV 16 / 24 / 32-bit float at the source's sample rate.
   Collision: Auto-number (`name (2).wav`), Overwrite or Skip. Tail (ms) records that much longer.
3. **Start** processes every Queued file top to bottom (Done files are skipped until you reset
   them: right-click > Reset status). **Pause** stops at once and records the interrupted file
   again from its start on **Resume**; **Skip** skips the current file; **Stop** ends the batch
   (the current file stays queued). Each result is written under a hidden temporary name and
   renamed when complete, so an interrupted take never leaves a half-written file.
4. The list shows status, progress and warning badges: **NC** not calibrated (latency estimated),
   **RS** resampled (the device could not run at the file's rate), **XR** dropout, **SIL** recorded
   silence?, **CLIP** clipped (hover for details). "Redo files with warnings" (right-click, or the
   header button) queues the files with dropouts, silence or clipping again.
5. The device is switched to each file's sample rate when it supports it (consecutive files with
   the same rate need no switch) and restored afterwards; otherwise the file is resampled there
   and back with a high-quality resampler and marked RS.

**Sidecar log.** Every batch writes `Reamp Forge batch <date> <time>.txt` into the destination
folder of its first file (the output folder itself in single-folder mode): device, rate, buffer,
channels, level, latency used (measured or estimated), format and naming, then one entry per file
(source → output, channel, samples, device rate, gain, latency, peak, warnings) as it finishes.

## Testing with the built-in mic and speakers

On macOS the built-in microphone and speakers are separate CoreAudio devices. Pick
"MacBook Pro Speakers" as the output device and "MacBook Pro Microphone" as the input device in
the AUDIO section; the app labels this pair "Not sample-synchronized, for testing only" because
two devices run on independent clocks. Use one interface (e.g. the Apollo) for real work.

Microphone access: macOS asks once. Until the prompt is answered the app does not open any
audio device that has inputs (CoreAudio would block the app until then), so answer it first.
If access is denied, playback and audition still work but every input is silent; allow Reamp
Forge in System Settings > Privacy & Security > Microphone and restart the app. When the app is
started from a terminal, macOS asks on behalf of the terminal app instead.

Set the output level low before the first Audition: it plays the selected file's channel through
the chosen output channel from the waveform's audition marker.

### Development flags

For one run only (nothing is saved): `--device=<name>` (input and output), `--output-device=`,
`--input-device=`, `--device-type=`, `--sample-rate=`, `--buffer-size=`, `--output-channel=` and
`--input-channel=` (1-based number or driver channel name), `--output-level=<dB>`, `--no-input`
(output-only devices, no microphone prompt), `--virtual-device` (a software device, no hardware
used, silent input), `--virtual-loopback[=n]` (the virtual device's output comes back on its
input one buffer + n samples later, default 300), `--virtual-rates=48000` (rates it offers, e.g.
to force resampling), `--virtual-speed=<x>` (run it x times faster than real time),
`--audition-check[=seconds]` (auditions the selected file, prints progress to stderr, exits 0 on
success), `--batch-check=<folder>` (runs a real batch of the opened files into `<folder>` on the
virtual loopback device, prints one line per file plus a verification of every output, exits 0
if all are exact; `--batch-check-transport` also exercises Pause/Resume, Skip and Stop;
`--batch-check-hardware` allows the selected real device, without the content check) and
`--sidebar-scroll=<section>` (for snapshots). Examples without touching any hardware:

```sh
APP="build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge"
"$APP" --virtual-device --open="$HOME/DI/Session A" --select="Riff 01.wav" --output-level=-30 --audition-check=2
"$APP" --open="$HOME/DI/Session A" --output-level=0 --virtual-speed=8 --batch-check=/tmp/reamp-check
```

See `Source/App/CommandLine.h` for details.

## Layout

```
CMakeLists.txt
Assets/Fonts/     JetBrains Mono + Inter (Regular/Medium/Bold) and their OFL licences
Source/Main.cpp   application entry point
Source/App/       main window, root component, Settings, AudioController, BatchController,
                  BatchLog, OutputOptions, microphone permission, command line, macOS
                  appearance, snapshot aid
Source/UI/        Theme (design tokens), Fonts, Format, LookAndFeel, TopBar, FileTreeView,
                  Sidebar + sections, Meters, WaveformPanel, StatusBar
Source/Engine/    AudioDeviceInterface, JuceAudioDevice, DeviceSession, DuplexEngine, RecordStream,
                  Take, FileWriter, Resampler, SourceLoader, LoopbackTestDevice (SyncMeasurer in phase 5)
Source/Model/     FileItem, FileTree, FolderScanner, BatchQueue, OutputNaming
Tests/            JUCE UnitTest runner and tests (FolderScanner, FileTree, FileTreeView, Settings,
                  DeviceSession, DuplexEngine, SourceLoader, Resampler, Loopback end to end, Take,
                  OutputNaming, BatchQueue) and a fake audio device
```

See `ARCHITECTURE.md` for the thread and data-flow design, `PROMPT.md` for the full
specification, and `docs/PROGRESS.md` for the current status and what comes next.

## Branches

- `develop`: default branch, all development happens here.
- `release`: deployed versions only. `develop` is merged into it and tagged when a version is
  handed over.

## Licences

- JUCE 9: AGPLv3 / commercial dual licence (used here under AGPLv3 for a personal tool).
- JetBrains Mono and Inter: SIL Open Font License 1.1 (see `Assets/Fonts/*-OFL.txt`).
