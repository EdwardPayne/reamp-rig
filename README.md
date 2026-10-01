# Reamp Rig

Batch re-amping of guitar DI tracks through a hardware amp via an audio interface.
Desktop app, C++20 + JUCE 9.0.3 (fetched automatically), CMake.

Status: **version 0.1.0, all six phases done** (phase 6 "polish" on 2026-10-01). What is still
open (mostly things that need the Apollo or a manual mouse pass) is listed in
`docs/PROGRESS.md` under "Open issues".

## Using Reamp Rig

Reamp Rig plays each DI file out of one output channel of your interface into the amp, records
the amp back on one input channel, and writes the result sample-aligned and exactly as long as
the source.

### 1. Setup

1. Open the app (`Reamp Rig.app`; the first time macOS asks for microphone access: allow it,
   the app cannot record any input otherwise).
2. **AUDIO**: pick the driver, the interface as output **and** input device, the sample rate and
   buffer, the **Output** channel that feeds the amp and the **Input** channel the amp comes back
   on (the names are the interface's own). Set **Output level** low and press **Audition**
   (Space) to hear the selected file through the amp; watch "Peak at output" and the meters.
3. Drop DI files or folders on the window (or **Add files…** / **Add folder…**). Folders are
   grouped by location; with **Include subfolders** on (OPTIONS) subfolders are scanned too,
   except folders named like the DESTINATION subfolder (`Reamped`): those hold results. Click
   **L** / **R** on stereo files to choose the channel that goes to the amp.

Everything you set is remembered, including the window's size and position. When the interface
is unplugged the app stops (a running batch pauses) and opens it again by itself as soon as it
is back.

### 2. Sync (latency calibration)

Do this once per interface setup, before the first batch, and again whenever the AUDIO section
says "NOT SYNCED" (top-bar chip):

1. **Bypass the amp.** Connect the chosen output channel directly to the chosen input channel
   with a cable (line out into line in; no amp, no pedals).
2. Leave **Sync level** at -12 dBFS (lower it if the result says "clipped", raise it or the
   interface's input gain if it says "level too low").
3. Press **Sync**. A click and a short sweep are played and recorded five times (about six
   seconds; the cells under the readouts fill as it goes, Stop cancels). The SYNC section then
   shows the round trip (`556 smp · 11.6 ms`), the returned peak, the confidence (high / medium /
   low; hover for the details) and the date, next to the driver's own figure for reference. The
   top-bar chip turns green with the measured value.
4. **Reconnect the amp** (output into the amp, the amp's mic or DI into the input) and run the
   batch.

A measurement belongs to the driver, the input and output device, the sample rate and the buffer
size; changing any of them shows "NOT SYNCED" until that configuration is measured (an earlier
measurement comes back when you switch back). The batch switches the device to each file's sample
rate, so measure every rate your files use: set the rate in the AUDIO section, press Sync, repeat.
If the batch would run in a configuration without a measurement, **Start** asks first ("Not synced
for this configuration", listing them); **Start anyway** uses the driver's estimate and marks those
files **NC**. Failures (nothing came back, no clear peak, level too low, clipped, not repeatable,
dropouts) are explained in the SYNC section and the status bar, and never replace a good
measurement. With the built-in speakers and microphone Sync measures through the room: it works at
a low level but is "not sample-synchronized" (separate devices), so expect medium or low
confidence.

**Forget** (next to Sync) deletes the measurement of the current configuration after asking, for
example after re-cabling; other configurations keep theirs. A device that stops during Sync ends
the measurement at once; nothing is stored.

### 3. Batch

1. Add files, pick the L/R channel of stereo files, set the AUDIO section (one interface for
   real work) and the output level (watch "Peak at output").
2. DESTINATION: **Subfolder next to source** (default, `<source folder>/Reamped/`) or **Single
   output folder** (choose it; "Mirror folder structure" recreates the folders below the folder
   you added, e.g. `<output>/Session A/Takes/`). Prefix and suffix make
   `<prefix><name><suffix>.wav` (example line underneath); OPTIONS "Append channel tag" adds
   `_L`/`_R` for stereo sources. Format WAV 16 / 24 / 32-bit float at the source's sample rate.
   Collision: Auto-number (`name (2).wav`), Overwrite or Skip. Tail (ms) records that much longer.
3. **Start** processes every Queued file top to bottom (Done files are skipped until you reset
   them: right-click > Reset status). Between two files the batch waits the **Pause between
   files** (OPTIONS, default 2 s) so the amp's and a reverb's tail dies out; the status line
   counts down ("File 4 of 7 — next in 2 s") and the ETA includes the waits. **Pause** stops at
   once and records the interrupted file again from its start on **Resume** (after one more
   pause); **Skip** skips the current file (during a wait: the file about to start); **Stop**
   ends the batch (the current file stays queued). Each result is written under a hidden
   temporary name and renamed when complete, so an interrupted take never leaves a half-written
   file.
4. The list shows status, progress and warning badges: **NC** not calibrated (latency estimated
   because the configuration had no sync measurement),
   **RS** resampled (the device could not run at the file's rate), **XR** dropout, **SIL** recorded
   silence?, **CLIP** clipped (hover for details). "Redo files with warnings" (right-click, or the
   header button) queues the files with dropouts, silence or clipping again, and NC files whose
   configuration has been synced since.
5. The device is switched to each file's sample rate when it supports it (consecutive files with
   the same rate need no switch) and restored afterwards; otherwise the file is resampled there
   and back with a high-quality resampler and marked RS.

**Sidecar log.** Every batch writes `Reamp Rig batch <date> <time>.txt` into the destination
folder of its first file (the output folder itself in single-folder mode): device, rate, buffer,
channels, level, latency used (measured or estimated), format and naming, then one entry per file
(source → output, channel, samples, device rate, gain, latency, peak, warnings) as it finishes.

### 4. Output

- Next to each source in `Reamped/` (default) or in one output folder you choose, named
  `<prefix><name><suffix>.wav` (default suffix `_reamp`), WAV 16 / 24 / 32-bit float at the
  source's sample rate, mono.
- One plain-text log per batch in the destination: `Reamp Rig batch <date> <time>.txt`, with the
  app version, the device, the latency used and every file's result and warnings.

### 5. Warnings and badges

| Badge | Meaning | What to do |
|---|---|---|
| **NC** | Not calibrated: no sync measurement for that device configuration, so the driver's latency estimate was used (may be off by a few ms) | Bypass the amp, press Sync for that rate, then "Redo files with warnings" |
| **RS** | Resampled: the device could not run at the file's rate; played and recorded at another rate and converted back (high quality) | Nothing, or use an interface that supports the rate |
| **XR** | Dropout during the take (xrun, callback gap or a full record buffer) | "Redo files with warnings"; close other apps or raise the buffer size |
| **SIL** | Recorded silence? (peak below -60 dBFS) | Check the amp, cables and the input channel, then redo |
| **CLIP** | The recording reached full scale | Lower the amp's output or the interface's input gain, then redo |
| **NOT SYNCED** (top bar) | The current configuration has no measurement | Sync, or Start anyway (files get NC) |
| **ERROR** (status) | The file could not be loaded or written; hover the status for the reason | Fix the cause, right-click > Reset status, Start |

Status-bar messages with a badge are warnings (amber) or errors (orange-red); hover the status bar
for details. A full disk or an unwritable output folder pauses the batch and says what to do; an
unplugged interface pauses it until it is back (then press Resume).

### 6. Keyboard shortcuts

| Key | Action |
|---|---|
| Space | Audition the selected file / stop (not while a batch runs or Sync measures) |
| L / R | Use the left / right channel of the selected stereo files |
| Delete or Backspace | Remove the selected files from the list |
| Cmd-A (Ctrl-A on Windows) | Select every visible file (files in collapsed folders are never selected) |
| Up / Down (Shift extends) | Move the selection |
| Return / Escape | Confirm / cancel the dialog (it ignores every other key) |

The list must have the keyboard focus for L, R, Delete and Cmd-A (click a file first). Collapsing
a folder deselects its files, so bulk actions never touch rows you cannot see.

# Development

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
build/ReampRig_artefacts/Release/Reamp Rig.app
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
open "build/ReampRig_artefacts/Release/Reamp Rig.app"
```

or, to see stdout/stderr in the terminal:

```sh
"build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
```

To add files or folders at startup (repeatable; folders follow the "Include subfolders" setting):

```sh
"build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig" --open="$HOME/DI/Session A" --open=take.wav
```

## Tests

JUCE `UnitTest` cases in `Tests/`, built as `ReampRigTests` with the app and run through ctest:

```sh
ctest --test-dir build --output-on-failure
```

### UI snapshot (development aid)

The app can render its own window (including open popup menus and tooltips) to a PNG and quit.
This works without granting the terminal screen-recording permission:

```sh
"build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig" --snapshot="$PWD/docs/phase6-empty.png"
```

The native macOS title bar is not part of the snapshot. It waits until scans and the waveform
have finished. Combine it with `--open=` and the development flags `--select=<file name>`
(repeatable, first one is shown in the waveform panel), `--audition-at=<seconds>` and
`--view=<start>:<end>` to capture a populated window (see `Source/App/CommandLine.h`).

## Testing with the built-in mic and speakers

On macOS the built-in microphone and speakers are separate CoreAudio devices. Pick
"MacBook Pro Speakers" as the output device and "MacBook Pro Microphone" as the input device in
the AUDIO section; the app labels this pair "Not sample-synchronized, for testing only" because
two devices run on independent clocks. Use one interface (e.g. the Apollo) for real work.

Microphone access: macOS asks once. Until the prompt is answered the app does not open any
audio device that has inputs (CoreAudio would block the app until then), so answer it first.
If access is denied, playback and audition still work but every input is silent; allow Reamp
Rig in System Settings > Privacy & Security > Microphone and restart the app. When the app is
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
`--sidebar-scroll=<section>` (for snapshots), `--sync-check` (runs Sync on the virtual loopback,
prints every repeat and the result, stores it, exits 0 when it matches the loop's true round trip;
`--sync-check-hardware` allows the selected real device, `--sync-check-rates=44100,96000` also
measures those rates, `--sync-level=<dBFS>` sets the level for the run; with `--batch-check` the
batch runs afterwards in the same process), `--virtual-reported-latency=<n>` (the virtual device
reports a wrong driver latency), `--settings-file=<path>` (use another settings file, so checks do
not touch yours), `--press-start` (presses Start after loading, e.g. to snapshot the "Not
synced" dialog), `--press-forget` (opens the Forget confirmation), `--window-bounds=x,y,w,h`
(moves/resizes the window after launch as if dragged; it is then remembered) and
`--virtual-unplug=<at>:<for>` (the virtual interface disappears `<at>` seconds after launch and
comes back `<for>` seconds later; with `--batch-check` the batch pauses, the device reopens by
itself and the check resumes). The pause between files is a normal setting
(`pauseBetweenFilesSeconds` in the settings file, OPTIONS in the app). Examples without touching
any hardware:

```sh
APP="build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
"$APP" --virtual-device --open="$HOME/DI/Session A" --select="Riff 01.wav" --output-level=-30 --audition-check=2
"$APP" --open="$HOME/DI/Session A" --output-level=0 --virtual-speed=8 --batch-check=/tmp/reamp-check
"$APP" --settings-file=/tmp/check.settings --virtual-loopback=1500 --sync-check
"$APP" --settings-file=/tmp/check.settings --open="$HOME/DI/Session A" --output-level=0 --virtual-loopback=300 \
       --virtual-reported-latency=100 --virtual-speed=8 --sync-check --sync-check-rates=44100,96000 \
       --batch-check=/tmp/reamp-check
"$APP" --settings-file=/tmp/check.settings --open="$HOME/DI/Session A" --output-level=0 --virtual-speed=4 \
       --virtual-unplug=4:2 --batch-check=/tmp/reamp-check
```

See `Source/App/CommandLine.h` for details.

## Known limits

- Windows is untested: it is expected to compile (ASIO needs `JUCE_ASIO=1` and Steinberg's SDK),
  but driver-type switching, ASIO/WASAPI and Sync there have never run.
- Resampled takes (RS) lose source content above about 0.447 of the lower sample rate
  (band-limiting by the high-quality resampler; real DI rarely has any).
- The batch keeps the played channel of the current and the next file in memory, audition the
  lead file's (4 bytes per sample; files over about 1 billion samples are refused).
- Dropout detection uses wall-clock callback spacing; on a heavily loaded machine with tiny
  buffers, scheduling jitter can be reported as a dropout (XR, warning only).
- At the maximum waveform zoom (50 ms visible) the thumbnail (64 samples per point) looks stepped.
- When the app is started from a terminal, macOS asks for microphone access on behalf of the
  terminal app.
- Built-in speakers + microphone are two devices with separate clocks: fine for trying the app,
  "not sample-synchronized" for real work.

## Layout

```
CMakeLists.txt
Assets/Fonts/     JetBrains Mono + Inter (Regular/Medium/Bold) and their OFL licences
Assets/Icon/      app icon PNGs (16-1024 px) and make_icon.py, which generates them
Source/Main.cpp   application entry point
Source/App/       main window, root component, Settings, AudioController, BatchController,
                  BatchLog, OutputOptions, SyncController, SyncPlan, microphone permission,
                  command line, macOS appearance, snapshot aid
Source/UI/        Theme (design tokens), Fonts, Format, LookAndFeel, TopBar, FileTreeView,
                  Sidebar + sections, Meters, WaveformPanel, StatusBar, ConfirmDialog
Source/Engine/    AudioDeviceInterface, JuceAudioDevice, DeviceSession, DuplexEngine, RecordStream,
                  Take, FileWriter, Resampler, SourceLoader, LoopbackTestDevice, SyncMeasurer
Source/Model/     FileItem, FileTree, FolderScanner, BatchQueue, OutputNaming
Tests/            JUCE UnitTest runner and tests (FolderScanner, FileTree, FileTreeView, Settings,
                  DeviceSession, DuplexEngine, SourceLoader, Resampler, Loopback end to end, Take,
                  OutputNaming, BatchQueue, Sync, Keyboard) and a fake audio device
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
