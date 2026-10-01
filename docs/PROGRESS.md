# Progress log

Single source of truth for where the project stands. Update at the end of every phase.
Phases are defined in `PROMPT.md` section 8; requirements are numbered per `PROMPT.md`.

## Status

| Phase | Scope | Status | Verified | Snapshot |
|---|---|---|---|---|
| 1 | Skeleton + theme | **Done** | 2026-09-30, clean Release build, launched, window captured | `docs/phase1.png` |
| 2 | Files + waveform | **Done** | 2026-09-30, clean Release build, 3/3 ctest entries pass, launched with `--open`, snapshots checked | `docs/phase2.png`, `docs/phase2-zoom.png` |
| 3 | Audio device layer | **Done** | 2026-09-30, clean Release build, 7/7 ctest entries pass (59 cases, 323 checks), launched on real CoreAudio devices, `--audition-check` passed on the virtual device, snapshots checked | `docs/phase3.png`, `docs/phase3-audition.png` |
| 4 | Engine + batch | Not started | | |
| 5 | Sync | Not started | | |
| 6 | Polish | Not started | | |

Environment used so far: macOS 26.6 (Apple Silicon), CMake 3.27.8, Apple Clang 21, Xcode
Command Line Tools only, no Ninja. JUCE 9.0.3 fetched by CMake. Clean build about 1 minute.

## Next up: phase 4 — engine + batch

Spec: `PROMPT.md` sections 3.3 (batch: Start / Pause-Resume / Stop / Skip, exact length, tail,
selected channel only, progress UI with "File 7 of 23 — 00:12 / 01:03 — ETA", lock-free FIFOs,
xrun reporting, silence/clip checks), 3.4 (destination modes, prefix/suffix + live example,
channel tag, WAV 16/24/32f at the source rate, collision policy, temp file + rename, sidecar
log), 4.2-4.6 (take inside the one duplex callback, latency compensation, sample-rate switching
or resampling with a "resampled" warning and grouping by rate, `LoopbackTestDevice`, writer
thread), 3.5.2 (recorded thumbnail under the source, current/last file only), and from section 7
the **end-to-end loopback test** (stereo and mono sources, buffers 64/256/480/1024, several
delays, output sample-exact in length and content, bit-exact at 0 dB) and the **OutputNaming**
tests. Persist the phase 4 keys (tail, prefix, suffix, destination mode, subfolder name, output
folder, mirror flag, channel tag, bit depth, collision policy) with round-trip tests.

Deliver:

- `Source/Engine/`: take mode in `DuplexEngine`, `Take`, `FileWriter` (writer thread, temp file
  and rename), `Resampler` (high quality, fractional delay accounted for), `LoopbackTestDevice`.
- `Source/Model/`: `BatchQueue`, `OutputNaming`.
- DESTINATION and OPTIONS sections live; top-bar Start / Pause / Stop wired; per-file status and
  progress in the list; recorded lane in the waveform panel; status line with ETA.
- Tests listed above, wired into ctest. Update this file, `ARCHITECTURE.md` (take data flow,
  writer thread, latency math), add `docs/phase4.png`.

Hooks left by phase 3 for phase 4:

- `engine::DuplexEngine` (`Source/Engine/DuplexEngine.*`) is the single duplex callback. Add the
  take next to audition: same immutable-command-through-an-atomic-pointer pattern (`Command`,
  `retire()`, `callbackCount`), source from sample 0 into the output index, input index into a
  lock-free FIFO (e.g. `juce::AbstractFifo` over a preallocated buffer), all other outputs
  already zeroed. Stop audition before a batch starts. `EngineSnapshot` is where progress, xrun
  counts and FIFO overflow flags go.
- `engine::SourceLoader` decodes one channel into a `LoadedSource` (immutable, shared) on its own
  thread; `loadAsync` cancels the previous request, so preloading the *next* file while the
  current one plays needs a second loader instance (or a queue). Its resampler is JUCE's
  windowed sinc with about -40 dB error on a test sine (fine for a level preview, see the
  `SourceLoader` test): the record path needs the proper `Resampler` from 4.4.
- `engine::AudioDeviceInterface` is what `LoopbackTestDevice` implements. `Tests/FakeAudioDevice.h`
  is a working template: it passes all channels to the callback and drives it with `render()`;
  the loopback device adds output-to-input with a configurable delay, gain and noise.
  `DeviceStatus` carries the driver-reported input/output latency (the "not calibrated" estimate
  and the sidecar log need it).
- Sample-rate switching (4.4): open the same config with another `sampleRate` through
  `DeviceSession::open` (`AudioController::applyConfig (wanted, false)` does that without
  persisting). The controller already reloads the lead when the device rate changes.
- `AudioController::isAuditioning()`, `stopAudition()`; `inputBlockedByPermission` tells whether
  the input is really open (a batch must refuse to start without it, with the same message).
- `WaveformPanel::setPlayhead (seconds)` for the batch playhead; `FileTree::setStatus` for rows.
- `TopBar::setSyncStatus (text, colour, tooltip)` and the unwired Start/Pause/Stop buttons.
- `Settings`: add typed accessors like `get/setOutputGainDb`; `Settings (juce::File)` for tests.
- `--virtual-device` (silent software device) and `--audition-check` show how to verify app
  paths without hardware; a batch check flag could reuse the pattern.

## Phase 3 — what was done (2026-09-30)

- `Source/Engine/`: `AudioDeviceInterface` (abstract: driver types, devices per direction,
  defaults, `open(DeviceConfig)`, `close`, `getStatus` → `DeviceStatus` with channel names,
  rates, buffer sizes, running config and driver latencies, `setCallback`, message-thread
  listener; `DuplexCallback` with input and output in one call plus a `StreamLayout`).
  `JuceAudioDevice` over `juce::AudioDeviceManager` (opens only the selected channels, packed
  channel mapping, no XML/default-device fallbacks, guard against an unrequested device).
  `DeviceSession` (resolution and fallback with plain-language warnings). `DuplexEngine`
  (audition playback and peak/clip meters, real-time safe). `SourceLoader` (decode the played
  channel, peak, resample to the device rate, on its own thread). No GUI dependency.
- AUDIO section: Driver, Output device, Input device (separate pickers; disabled for drivers
  without separate I/O), a two-line `warn` notice "Not sample-synchronized, for testing only" when
  they differ, Sample rate, Buffer, Output / Input channel combos with the driver's channel names,
  OUT and IN peak meters (custom `LevelMeter`, clip latches in `warn` until clicked), Output level
  -60..+12 dB (double-click 0 dB), "Peak at output" (file peak + level, `warn` + "clips" above
  0 dBFS), Audition / Stop. All themed through `ForgeLookAndFeel` and the new components.
- Audition: plays the lead file's chosen channel from the waveform's audition marker through the
  selected output channel with the gain applied, nothing recorded; Stop, Space, end of file, a
  new lead or a channel change stop it. Accent playhead moves in the waveform panel and the
  header readout follows it. Source preloaded on the loader thread; the callback only copies
  with gain; all other outputs are zeroed.
- Top bar: live device summary (`Apollo Twin · 48 kHz · 256`, `Out: … · In: … · …` for split
  devices, "No device" when closed) and sync chip ("No device" muted / "Not synced" warn).
  SYNC section shows the driver-reported latency (`in N + out M smp`).
- Persistence: device type, input/output devices, sample rate, buffer size, input/output channel
  (index + driver name), output gain. Restored on launch; a missing device/type/channel/rate
  falls back with a status-bar warning and the fallback is not saved (the saved device is used
  again when it is back). User changes are saved immediately.
- macOS microphone permission (`App/MicrophonePermission.mm`, AVFoundation): see "Deviations" 6.
  Denied: device opens without input; status-bar error with the System Settings path.
- Device disconnect while open: audition stops, status-bar warning, UI shows "No device".
- Dev flags (not persisted): `--device-type`, `--device`, `--output-device`, `--input-device`,
  `--sample-rate`, `--buffer-size`, `--output-channel` (number or name), `--input-channel`,
  `--output-level`, `--no-input`, `--virtual-device`, `--audition-check[=sec]`
  (`Source/App/CommandLine.h`). The app logs the enumerated devices at startup.
- Tests: `Settings` (defaults, round-trip of every key incl. split devices and non-ASCII names,
  clamping, garbage values), `DeviceSession` (channel resolution by name/index, packed index
  mapping, exact restore, reordered channels, missing device/type, first launch, input follows
  output, unsupported rate/buffer, input without permission, open failure fallback, nothing
  opens, no devices, ASIO-style drivers), `DuplexEngine` (audition exact on the chosen channel of
  4 with gain and start offset at buffers 64/256/480/1024, other channels zero, end of file,
  stop + release, gain ramp, meters and latching clip, invalid starts), `SourceLoader` (channel
  decode, peak, resample length and alignment, clamping, junk/missing file, abort).
  59 test cases, 323 checks.

Verification commands (repository root; `SP` = the session scratchpad with the phase 2 audio):

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"   # no errors, no warnings from our code
ctest --test-dir build --output-on-failure                       # 7/7 passed
APP="build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge"
osascript -e 'set volume output volume 0'                        # safety backstop, restored to 31 afterwards
# 1. Real CoreAudio devices, virtual Teams device, 5 s, quit via AppleScript -> exit 0:
"$APP" --open="$SP/audio/Session A" --device="Microsoft Teams Audio" --output-level=-60
#    stderr: Audio devices (CoreAudio): outputs [MacBook Pro Speakers, Microsoft Teams Audio],
#            inputs [MacBook Pro Microphone, Marcus iPhone 16 Microphone, Microsoft Teams Audio]
#            Requesting microphone access
# 2. Audition end to end on the silent virtual device (exit 0):
"$APP" --virtual-device --open="$SP/audio/Session A" --select="Riff 01.wav" --audition-at=3.2 \
       --output-channel=3 --output-level=-30 --audition-check=1.5
#    [audition-check] OK: played 1.57 s of Riff 01.wav in 1.54 s; output peak -38.1 dBFS
#    (file peak with gain -34.3 dBFS); input peak -100.0 dBFS
# 3. Snapshots:
"$APP" --open="$SP/audio/Session A" --open="$SP/audio/Session B" --select="Riff 01.wav" --audition-at=3.2 \
       --output-device="MacBook Pro Speakers" --no-input --output-level=-60 --snapshot="$PWD/docs/phase3.png"
"$APP" --virtual-device --open="$SP/audio/Session A" --open="$SP/audio/Session B" --select="Riff 01.wav" \
       --audition-at=3.2 --output-channel=3 --output-level=-12 --audition-check=6 \
       --snapshot="$PWD/docs/phase3-audition.png"
```

Devices opened during verification: "Microsoft Teams Audio" (virtual; Teams itself was not
running), "MacBook Pro Speakers" output-only for the snapshot with the level at -60 dB, no
audition and the system volume at 0. **Nothing was played through the speakers or any physical
output**; audition was verified on the virtual device and in the unit tests.

Deviations from the spec (phase 3):

1. Audition plays at the device's current rate: if the file's rate differs, the loader resamples
   the preview (the status line says "resampled from …"). Switching the device rate per file
   (4.4) is left to the batch in phase 4.
2. Output and Input device are two combos on every platform; for drivers without separate I/O
   (ASIO) the input follows the output and its combo is disabled. The split notice wraps onto
   two lines to fit the 280 px sidebar.
3. More development flags (list above), all in `Source/App/CommandLine.*`, none persisted.
4. `App/VirtualAudioDevice` (development aid, `--virtual-device`): a silent software device
   with a paced thread so the full app path can be checked without hardware, speakers or a
   permission prompt. Not the phase 4 `LoopbackTestDevice` (no loop, no delay model).
5. The SYNC section's "Driver" readout already shows the driver-reported latency (3.6.5), since
   the device layer provides it; measuring stays in phase 5.
6. **Microphone permission gates the device, not just the input.** JUCE 9 opens every CoreAudio
   device through a private aggregate carrying all of its streams, so creating any device that
   has inputs (even output-only) blocks inside coreaudiod until the macOS prompt is answered.
   While access is undetermined the app therefore opens nothing, says "Waiting for microphone
   access" in the status bar and top bar, asks without blocking and opens the device once the
   user answers (`--no-input` skips this for output-only devices).
7. Only the selected input and output channel are opened; unopened outputs are silent (CoreAudio
   writes only open channels) and the engine zeroes every buffer it is handed.
8. Channels are persisted as index plus driver name (`inputChannelName`, `outputChannelName`),
   so a reordered driver channel list still finds the right channel.
9. JUCE's `AudioDeviceManager` holds its own `audioCallbackLock` around our callback (only
   contended while the device is reconfigured). Our callback code takes no locks.
10. Clip threshold is 0.9999 (full scale within 24-bit rounding).
11. Buttons no longer take keyboard focus when clicked (app-wide), so Space and the list
    shortcuts keep working after clicking a button.

## Phase 2 — what was done (2026-09-30)

- `Source/Model/`: `FileItem` (id, file, header info, channel, status, progress), `FileTree`
  (groups by parent folder in order of first appearance, natural name order inside a group,
  dedupe by absolute path, selection + lead item, L/R rule, remove, reset status, listener),
  `FolderScanner` (recursive on/off, dedupe, skips and reports unreadable files, supported
  formats = `registerBasicFormats()`, synchronous `scan()` plus `scanAsync()` on its own thread
  with delivery on the message thread).
- Drag-and-drop of files and folders anywhere on the window (accent outline while dragging), plus
  "Add files…" and "Add folder…" (native file dialogs) in the FILES header.
- `rf::app::Settings` over `juce::ApplicationProperties`; "Include subfolders" (default on) is
  read from and written to it. Verified by editing the settings file: with it off, a nested
  folder is not scanned.
- `FileTreeView`: custom-painted list: column header; collapsible folder groups (chevron, path
  with `~` for home and start-truncation, file count); file rows with name, Mono/Stereo,
  duration, sample rate, bit depth, L/R selector (accent fill + black text when active,
  disabled look for mono), status (colour + badge for Error), static 4 px progress bar. Selected
  rows use panel-2 with a 2 px accent left edge; hover uses panel-2. Click, shift-click (range
  over visible rows), cmd-click, cmd-A, L, R, Delete/Backspace (selects the next file), Up/Down
  (shift extends), right-click menu (Use left/right channel, Reset status, Remove). Clicking L/R
  on a selected row applies to every selected stereo file. Live "N files · M selected" in the
  header and "N files queued" in the status bar. Drop hint until the first file is added.
- `WaveformPanel`: `AudioThumbnail` + `AudioThumbnailCache` (thumbnail data built on the cache's
  thread), one lane per channel for stereo with the non-played channel dimmed and L/R labels,
  one lane for mono, ruler that follows zoom (1-2-5 steps, labels that do not fit are dropped),
  cmd+scroll / pinch zoom around the mouse, plain scroll and a themed scrollbar to pan when
  zoomed, click/drag to set the audition start marker (accent line + flag), readout
  `start / duration`, file name, channel count and rate in the header, "Building waveform N%"
  while loading. RECORDED lane is still a placeholder.
- Status bar: message with warning/error tone (badge + colour), full detail in its tooltip;
  scan results ("Added 7 files. Skipped junk.wav (not a readable audio file)").
- Command line: `--open=<path>` (repeatable). Development aids for snapshots: `--select=`,
  `--audition-at=`, `--view=`; `--snapshot` now waits until scans and the waveform are done.
- Tests (JUCE `UnitTest`, choice documented in `ARCHITECTURE.md`): `FolderScanner` (recursion
  on/off, order, header facts, dedupe, junk file skipped and reported, explicit non-audio and
  missing files, no read permission, abort, grouping by folder, FileTree dedupe, stable order),
  `FileTree` (defaults, multi-select L/R rule incl. mono and 4-channel files, selection, reset
  status, remove, re-add), `FileTreeView` (headless keyboard: cmd-A, L/R, Up/Down, Backspace,
  Delete). 26 test cases, 131 checks.

Verification commands (all run from the repository root):

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"   # no errors, no warnings from our code
ctest --test-dir build --output-on-failure                       # 3/3 passed
python3 <scratchpad>/make_audio.py <scratchpad>/audio            # 2 folders + nested, mono/stereo,
                                                                 # 44.1/48/96 kHz, 16/24/32f, junk.wav
APP="build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge"
"$APP" --open="<audio>/Session A" --open="<audio>/Session B" --select="Riff 01.wav" \
       --select="Riff 02.wav" --select="Take 02.wav" --audition-at=3.2 --snapshot="$PWD/docs/phase2.png"
"$APP" --open="<audio>/Session A" --select="Riff 01.wav" --view=2.9:3.6 --audition-at=3.2 \
       --snapshot="$PWD/docs/phase2-zoom.png"
```

The real app was also launched with `--open` on both folders, stayed up, logged
`Skipped …/junk.wav: not a readable audio file`, and quit cleanly (exit 0) via AppleScript.

Deviations from the spec (phase 2):

1. Tests use JUCE `UnitTest` instead of Catch2 (no extra dependency). The test executable also
   compiles `FileTreeView`, `LookAndFeel` and `Fonts` for the headless keyboard test.
2. More development flags next to `--snapshot`: `--select`, `--audition-at`, `--view`
   (`Source/App/CommandLine.h`). `--open` is a user-facing option.
3. `AudioThumbnail::setSource` reads the file header on the message thread (JUCE design); the
   level data is generated off it. The scanner reads headers only, never decodes audio.
4. The Add files/folder dialogs are the native macOS panels, which cannot be themed (like the
   title bar).
5. While recursing, hidden files and symbolic-linked subfolders are skipped; files with a
   non-audio extension inside scanned folders are ignored silently. Only audio-extension files
   that fail to open, explicitly given non-audio files, missing paths and unreadable folders are
   reported.
6. Files with more than two channels show "N ch" and offer L/R for their first two channels.
7. Small additions: Up/Down (shift extends) in the list, "· N selected" in the FILES header,
   skipped files are also written to the log (stderr).
8. `Source/Model` uses `juce_events` (`MessageManager::callAsync`) to hand scan results to the
   message thread; still no GUI code.

## Phase 1 — what was done (2026-09-30)

- CMake project, JUCE 9.0.3 via `FetchContent` (release tarball pinned by SHA256).
- App "Reamp Forge", bundle id `com.reampforge.app`, min window 1100×700, ad-hoc signed as a
  post-build step, `NSMicrophoneUsageDescription` in Info.plist.
- Full layout from `PROMPT.md` section 5 with placeholder controls: top bar (name, device
  summary, sync chip, Start/Pause/Stop), file area with empty-state hint, 280 px scrolling
  sidebar (AUDIO, SYNC, DESTINATION, OPTIONS), waveform panel with source and recorded lanes
  behind a splitter, status bar.
- `ForgeLookAndFeel` applied app-wide: buttons, toggles, combo boxes and popup menus, sliders,
  text editors, labels, scrollbars, tooltips, progress bars, splitter. Every generic or system
  font request is mapped to the embedded faces.
- JetBrains Mono 2.304 and Inter 4.1 (Regular, Medium, Bold) embedded with their OFL licences.
- Audio modules (`juce_audio_devices`, `juce_audio_formats`, `juce_audio_utils`) linked to
  prove they build. No engine code.

Deviations from the spec, all accepted:

1. `--snapshot=<file.png>` development flag (`Source/App/Snapshot.*`) renders the window,
   popups and tooltips to PNG and quits. Added because `screencapture` initially failed without
   Screen Recording permission. Keep; it is the standard way to verify UI phases.
2. Dark window appearance forced on macOS (`Source/App/MacAppearance.mm`) so the title bar is
   dark in light mode too.
3. Tooltips are drawn as a child of the main component instead of an OS window, because JUCE
   forces a drop shadow on tooltip windows and the spec forbids shadows.
4. Sidebar runs full height beside both the file tree and the waveform panel; status bar spans
   the full width. Waveform panel keeps its 220 px height on resize.
5. JUCE 9 is AGPLv3/commercial, not GPL as the spec first said. Spec corrected.

## Open issues

- **Pending macOS microphone prompts.** Two prompts were shown during verification (TCC log,
  23:51 and 23:59), both for **Terminal**, because apps launched from a shell are attributed to
  their responsible process. They could not be answered here. Allowing Terminal lets dev runs
  from Terminal record; launched from Finder (`open …app`) the app is asked under its own name.
  `tccutil reset Microphone com.apple.Terminal` undoes a decision.
- **Audition never played on real hardware** (instruction: speakers silent until the owner's
  listening test). Verified with the fake device (sample-exact), the virtual device (whole app
  path, `--audition-check`) and by opening real devices silently. First real test: set the level
  low, pick the device, press Audition.
- **Input meter never saw a real signal** (no microphone access in this session).
- **Mouse interactions not exercised automatically.** Click, shift/cmd-click, clicking L/R,
  the context menu, collapsing groups, drag-and-drop from Finder, scroll/pinch zoom, dragging the
  marker and the Add dialogs could not be driven: this Mac does not allow synthetic input
  (`osascript` keystrokes refused, error 1002) or screen capture. Phase 3 adds: the AUDIO combos
  and their popups, the level slider, clicking a meter to clear CLIP, the Audition button and
  Space (window-level key handler; tested only through code review). Needs a manual pass.
- When the open device disappears the app stops and warns but does not switch back by itself
  when the device returns; pick it again (or restart).
- Driver type switching on Windows: JUCE opens the new type's default devices for a moment
  (the callback is not forwarded to them). ASIO/WASAPI never tested.
- The audition preview holds the played channel of the lead file in memory (4 bytes per sample;
  files over about 1 billion samples are refused with a status message).
- Files inside a collapsed group stay selected, so L/R, Delete and Reset status apply to them
  even though they are hidden (same as Finder). Revisit if it confuses.
- At the maximum zoom (50 ms visible) the thumbnail (64 samples per point) looks stepped.
  Fine for level checks; raise the resolution if phase 5 wants to inspect sync visually.
- **Broken Command Line Tools on the development Mac.** A stale, partial
  `/Library/Developer/CommandLineTools/usr/include/c++/v1` hides the SDK's libc++ headers.
  `CMakeLists.txt` detects this and adds `-nostdinc++ -isystem <SDK>/usr/include/c++/v1` with
  a warning. Permanent fix is reinstalling the Command Line Tools or removing that folder.
  Not done; the owner decides.
- Window size and position are not remembered yet (phase 6).
- Windows build has never been attempted (spec: compile-only expectation until later).

## Decision log

- 2026-09-30: JUCE over Tauri/Rust. Reason: a single duplex callback with a fixed in/out
  relationship is essential for sample-accurate sync; cpal has no merged synchronized CoreAudio
  duplex stream. See `PROMPT.md` section 2.
- 2026-09-30: JUCE 9.0.3 rather than 8.x, because it built cleanly with the local toolchain.
- 2026-09-30: Visual reference is https://plugins.omarchy.org ; tokens extracted from its CSS
  and fixed in `PROMPT.md` section 5.
- 2026-09-30: Branch model: `develop` default, `release` for deployed versions only.
- 2026-09-30: Tests use JUCE `UnitTest` (no Catch2), one ctest entry per category.
- 2026-09-30: On macOS no audio device is opened while microphone access is undetermined
  (JUCE 9 aggregate devices make coreaudiod block on the prompt). See phase 3 deviation 6.
