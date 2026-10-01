# Reamp Rig — implementation prompt

You are implementing **Reamp Rig**, a desktop application for batch re-amping guitar DI (direct input) tracks through a hardware guitar amplifier via an audio interface. Read this whole document before writing any code. Decisions marked **DECIDED** are final; do not re-open them. If you are genuinely blocked, ask one precise question. Otherwise proceed.

The working directory is empty except for `idea.txt` (the original idea) and this file. There is no git repository and none is required for now.

---

## 1. What the program does

The user has a folder (or folders) of DI guitar recordings. The program plays each file, one at a time, out of a chosen output channel on the audio interface. That output is cabled into a guitar amp, whose output is mic'd or DI'd back into a chosen input channel on the same interface. The program records that input for the exact length of the source file and writes the result to disk with a configurable prefix/suffix. The program knows nothing about the amp. It only needs the interface, one output channel and one input channel.

Primary hardware: a Mac with a Universal Audio Apollo (Thunderbolt). During initial development no interface is connected, so the built-in microphone and speakers must be usable for testing (they are separate CoreAudio devices on macOS; see 4.7).

---

## 2. Platform and stack — DECIDED

- **Language/framework: C++20 with JUCE (latest stable 8.x, or 9.x if the toolchain builds it cleanly). Build with CMake, fetch JUCE with `FetchContent`** so the project builds from a clean checkout with only CMake, a C++ compiler and (on Mac) Xcode command line tools installed.
- **Why JUCE, not Tauri/Electron/Rust+cpal:** the hard part of this app is sample-accurate, simultaneous play+record on one device. JUCE delivers input and output buffers in **one callback** (`AudioIODeviceCallback`) with a fixed, deterministic in/out relationship per device configuration, on CoreAudio (Mac) and ASIO/WASAPI (Windows). It also gives us driver-reported channel names, driver-reported latency, audio-file readers/writers, waveform thumbnails, native file drag-and-drop, and settings persistence in one dependency. Rust's `cpal` still has no merged synchronized CoreAudio duplex stream and ASIO support there is immature, so a Tauri build would put the riskiest part of the project on the weakest foundation.
- **Cross-platform:** design and structure the code so it builds on macOS and Windows. **Develop and verify on macOS first.** Keep Windows-specific code isolated. Windows builds are expected to compile but are untested for now. Note for later: on Windows, the Apollo uses ASIO, which requires `JUCE_ASIO=1` and Steinberg's ASIO SDK on the build machine; WASAPI is the fallback.
- **App type:** a single-window GUI app (not a plugin, not a CLI). App name `Reamp Rig`, bundle id placeholder `com.reamprig.app`.
- **Fonts:** embed JetBrains Mono and Inter (both SIL OFL) as binary data via `juce_add_binary_data`, so the look is identical on every machine.
- **Licenses:** only permissive dependencies (JUCE under its AGPLv3/commercial dual license is acceptable for this personal tool; fonts OFL). No other third-party libraries unless there is a strong reason; state it if you add one.

---

## 3. Functional requirements

### 3.1 Getting files in
1. Drag and drop **files and/or folders** from Finder/Explorer onto the window. Also provide "Add files…" and "Add folder…" buttons.
2. Checkbox **"Include subfolders"** (persisted). When on, dropped/added folders are scanned recursively.
3. Supported formats: WAV, AIFF, FLAC, and anything else JUCE's basic format manager reads. Skip unreadable files with a visible warning; never crash on a bad file.
4. The file list is a **tree grouped by folder**. Each folder is a collapsible group header with a path and a file count; files are rows under it. Dropping the same file twice must not create a duplicate.
5. Each file row shows: file name, channel count (Mono / Stereo), duration, sample rate, bit depth, the **L/R channel selector** (stereo only), status (Queued / Recording / Done / Skipped / Error), and a per-file progress bar while it is being processed.
6. **Multi-select** rows (shift-click, cmd/ctrl-click, cmd/ctrl-A). With several rows selected, choosing L or R applies to **all selected stereo files**. Mono files ignore the choice and show the selector disabled. Default for stereo files: L. Selection also drives "Remove selected" and "Reset status of selected".
7. Selecting a row shows its **waveform** in the waveform panel (see 3.5).

### 3.2 Audio device setup
1. Device selection: audio device type (CoreAudio / ASIO / WASAPI), the device, sample rate and buffer size. On macOS allow separate input and output devices so the built-in mic and speakers can be used for testing; the app must clearly label that setup as "not sample-synchronized, for testing only".
2. **Output channel** and **input channel** selectors must show the **names reported by the driver** (e.g. `Apollo Line 3`, `Mic/Line 1`), not just numbers. Output is a single mono channel. Input is a single mono channel.
3. Live **input meter** and **output meter** (peak, with a clip indicator that latches until clicked).
4. **Output level** control in dB (range −60 to +12, default 0, persisted), applied to playback of every file. Show the resulting peak of the current source file so the user can see whether the amp will be driven too hard.
5. **Audition** button: play the selected file through the chosen output without recording, so the user can set amp levels. Stoppable at any time.

### 3.3 Batch processing
1. Buttons: **Start**, **Pause/Resume**, **Stop**. Start processes every Queued file top-to-bottom in list order. Done files are skipped unless the user resets them. A "Skip current file" button.
2. For each file: play it through the output channel with the gain applied, and simultaneously record the input channel. The output file must have **exactly the same number of samples as the source** (see 4.3 for how latency compensation makes this true). Optional **tail** setting in ms (default 0, persisted) that appends extra recorded time after the source ends; with tail = 0 the length is exactly the source length.
3. Playback of stereo sources uses the selected channel only. Recorded output is mono.
4. During processing the list shows the current file highlighted, its progress bar, the waveform panel shows a moving playhead over the source waveform, and a status line shows "File 7 of 23 — 00:12 / 01:03 — ETA 14:20".
5. Never drop or glitch audio: all file I/O, decoding and encoding happen off the audio thread. Preload the next file while the current one is being recorded. Use lock-free FIFOs between the audio thread and the writer thread. The audio callback must not allocate, lock, or touch the filesystem.
6. Detect and report **xruns/dropouts** during a take (JUCE exposes device callback timing; also detect callback gaps). If one occurs, mark the file with a warning and give the user a "Redo files with warnings" action.
7. **Silence/level sanity check** per file: if the recorded peak is below a threshold (e.g. −60 dBFS) mark it "Recorded silence?" as a warning; if it clipped, mark "Clipped". Neither stops the batch.

### 3.4 Output files
1. **Destination mode** (persisted), radio choice:
   - **Subfolder next to source** (default): write into `<source folder>/<subfolder name>/`, subfolder name editable, default `Reamped`.
   - **Single output folder**: a chosen folder; optionally mirror the relative folder structure of the sources under it (checkbox, default on).
2. **Prefix** and **suffix** text fields (persisted), applied as `<prefix><original name><suffix>.<ext>`. Live example of the resulting name shown under the fields.
3. Optional toggle to append the chosen channel (`_L` / `_R`) for stereo sources (default off).
4. Format: WAV, bit depth selectable 16 / 24 / 32-float (default 24), sample rate = source sample rate.
5. **Collision policy** (persisted): Overwrite / Skip / Auto-number (`name (2).wav`). Default Auto-number.
6. Write to a temp file and rename on completion so an interrupted take never leaves a half-written file with the final name.
7. Write a small sidecar log per batch (plain text, in the destination) listing source → output, channel used, gain, measured latency, device/sample rate/buffer, and any warnings.

### 3.5 Waveform panel
1. Shows the selected file's waveform (per channel for stereo, with the non-selected channel dimmed). Time ruler, zoom with scroll/pinch, click to position an audition start point.
2. During and after recording, show the **recorded result underneath the source on the same time axis**, so the user can visually confirm sync and level. Keep the recorded thumbnail in memory only for the current/last file; do not keep all of them.
3. Rendering must stay smooth for files of any length (use `juce::AudioThumbnail` with a cache, generate off the message thread).

### 3.6 Sync (latency calibration)
1. **Sync** button, with an explanatory line in the UI: *"Connect the output directly to the input (bypass the amp) before measuring."*
2. Measurement: play a short, known test signal (a click plus a ~50 ms exponential sine sweep, at a user-adjustable level, default −12 dBFS) and record for ~1 s. Compute the round-trip delay by cross-correlating the recording with the emitted signal; take the peak. Repeat 5 times, discard outliers, use the median. Report the result in **samples and ms**, plus the returned peak level, and a confidence indicator (peak-to-sidelobe ratio and repeatability within ±1 sample).
3. Fail clearly (no peak, level too low, clipped) with a plain-language message. Never store a bad measurement.
4. Store the measured latency **keyed by device type + device name(s) + sample rate + buffer size**. When the current configuration has no stored measurement, or it differs from the stored one, show a prominent warning next to Start ("Not synced for this configuration") and require confirmation to start anyway (then use driver-reported latency as an estimate and mark all files with a "not calibrated" warning in the log).
5. Show the driver-reported input+output latency alongside the measured value for reference.

### 3.7 Persistence
Remember between launches (JUCE `ApplicationProperties`, per-user settings): device type, input/output devices, sample rate, buffer size, input channel, output channel, output gain, tail, prefix, suffix, destination mode, subfolder name, output folder, mirror-structure flag, channel-tag toggle, bit depth, collision policy, include-subfolders, sync measurements (keyed as in 3.6.4), window size/position. Do **not** persist the file list.

---

## 4. Audio engine design — DECIDED

4.1 Put all audio logic in an `Engine` module with **no dependency on the GUI**. The GUI observes engine state through a thread-safe snapshot/listener mechanism; it never reaches into engine internals.

4.2 The engine runs a **single duplex callback** on the device. Inside one callback it (a) copies the next block of the source into the selected output channel with gain, and (b) copies the selected input channel into the record FIFO. All other channels are zeroed.

4.3 **Latency compensation:** when a take starts, the engine starts writing source samples at output sample index 0 and starts capturing input at the same callback. It records `sourceLength + latency + tail` samples, then **discards the first `latency` recorded samples**. Result length = `sourceLength + tail`. With tail = 0 this is exactly the source length, sample-aligned to the source.

4.4 **Sample rate:** before each file, if the device's current sample rate differs from the file's, attempt to switch the device to the file's rate. If the device does not support it, resample the source for playback and resample the recording back to the source rate (high quality, and with the fractional delay accounted for), and mark the file with a "resampled" warning. Group consecutive files by sample rate to minimize device switches. Any change of sample rate or buffer size re-checks the stored sync value (3.6.4).

4.5 **Testability:** define the engine against an abstract audio-device interface so tests can run it with a **simulated loopback device** that feeds output back to input with a configurable delay and optional gain/noise. Use this to prove, in an automated test, that a processed file comes back sample-exact after sync (bit-exact for gain 0 dB and delay N, for several N and buffer sizes including non-power-of-two).

4.6 Ownership/threads: audio thread (callback only), a writer thread (drains the record FIFO into the file writer), a loader thread (decodes upcoming files, builds thumbnails), the message thread (UI). Document this in a short `ARCHITECTURE.md`.

4.7 macOS specifics: add `NSMicrophoneUsageDescription` to the Info.plist (the app will not receive input otherwise), request microphone permission on first use of input, and handle the "denied" state with a clear message. Sign ad-hoc for local runs.

---

## 5. Visual design — DECIDED

The look must match the aesthetic of https://plugins.omarchy.org : a terminal-inspired, high-contrast, sharp-cornered dark UI. Implement it as a custom `juce::LookAndFeel_V4` subclass applied app-wide, and build custom components where the stock ones cannot be styled to match (e.g. the device selector, the file tree, meters, waveform). No stock JUCE look may leak through (no rounded pill buttons, no gradients, no default fonts).

Design tokens (use these exact values, centralized in one `Theme` header):

| Token | Value | Use |
|---|---|---|
| bg | `#000000` | window background |
| panel | `#0b0b0d` | cards, list background, waveform background |
| panel-2 | `#1a1a1a` | hover rows, secondary surfaces |
| line-soft | `#232323` | subtle separators |
| line | `#28282c` | card and control borders (1 px) |
| line-strong | `#7a7a7a` | focused control border |
| text | `#d7d7d9` | body text |
| heading | `#eeeeee` | headings and primary values |
| faint | `#a8a8a8` | secondary labels |
| muted | `#8d8d8d` | disabled text, ruler ticks |
| accent | `#ff5a36` | primary action button fill (black text on it), playhead, selected-channel highlight, progress fill |
| ok | `#b4c96f` | Done status, sync OK |
| warn | `#ffb000` | warnings, clip indicators, "not synced" |
| error | `#ff5a36` | errors (same hue as accent, with an icon/label so it is not ambiguous) |

Typography: JetBrains Mono for all headings, section labels, buttons, values (dB, ms, samples, times) and table cells; Inter for prose/help text. Section labels are **small uppercase mono with letter-spacing** (e.g. `INPUT`, `OUTPUT`, `DESTINATION`, `SYNC`), as on the reference site. **Zero border radius everywhere.** 1 px borders, no drop shadows, no gradients. Spacing on an 8 px grid. Primary button: accent fill, black uppercase mono text. Secondary button: transparent with `line` border, text color `text`, hover to `panel-2`. Focus rings use `line-strong`.

Layout (single window, min 1100×700, resizable, remembered):
- Top bar (44 px): app name in mono, device summary (`Apollo Twin · 48 kHz · 256`), sync status chip (OK / NOT SYNCED / measured value), and Start / Pause / Stop.
- Left: the file tree (≈ 55% width) with a drop-zone hint when empty ("Drop DI files or folders here").
- Right: a sidebar (280 px) with stacked sections: Audio (device, in/out channel, meters, gain, Audition), Sync, Destination (mode, subfolder/folder, prefix/suffix, format, collision), Options (include subfolders, tail, channel tag).
- Bottom: waveform panel (≈ 220 px, resizable via a splitter) with source above and recorded result below, transport info and the status line.

Every interactive control needs a tooltip. Keyboard: Space = audition selected / stop, Delete = remove selected, cmd/ctrl-A = select all, L / R = set channel on selection.

---

## 6. Project structure

```
reamp-rig/
  CMakeLists.txt
  README.md                 build/run instructions, how to test with built-in mic/speakers
  ARCHITECTURE.md           threads, data flow, sync math
  Assets/Fonts/             JetBrainsMono-*.ttf, Inter-*.ttf (+ OFL licenses)
  Source/
    Main.cpp
    App/                    settings, main window, keyboard commands
    Engine/                 AudioDeviceInterface, DuplexEngine, Take, SyncMeasurer, Resampler, FileWriter, LoopbackTestDevice
    Model/                  FileItem, FileTree, BatchQueue, OutputNaming, FolderScanner
    UI/                     Theme, LookAndFeel, TopBar, FileTreeView, Sidebar sections, WaveformPanel, Meters, StatusBar
  Tests/                    Catch2 or JUCE UnitTest based; run via ctest
```

---

## 7. Tests (must exist and pass)

- SyncMeasurer: recovers a known delay from a synthetic loopback at several delays, with added noise, with gain change, with a clipped return; rejects a silent return.
- End-to-end engine test with `LoopbackTestDevice`: stereo and mono sources, several buffer sizes (64, 256, 480, 1024), several delays; output file is sample-exact in length and content after compensation.
- FolderScanner: recursion on/off, dedupe, unreadable files skipped, grouping by folder.
- OutputNaming: prefix/suffix, channel tag, collision policies, destination modes with mirrored structure.
- Settings round-trip including the keyed sync store.

---

## 8. Way of working

Deliver in phases. After each phase, stop, summarize what works, how to run it, and any deviations; then continue on approval.

1. **Skeleton + theme:** CMake project builds and launches an empty window with the full look-and-feel applied, fonts embedded, top bar and sidebar layout in place with placeholder sections. README with build commands.
2. **Files + waveform:** drag-and-drop, folder scanning, tree view with grouping, multi-select, L/R selection, waveform panel with thumbnail generation.
3. **Audio device layer:** device/channel selection with driver names, meters, audition, persistence.
4. **Engine + batch:** duplex take, latency compensation, writer thread, destination/naming, collision handling, progress UI, sidecar log, loopback tests.
5. **Sync:** measurement, keyed storage, warnings, driver-latency display.
6. **Polish:** tooltips, keyboard shortcuts, error states, dropout detection, xrun redo, window state, final pass on visuals against the reference site.

Rules:
- Verify each phase by building and launching the app, not just by compiling. Report the exact commands used and the actual output.
- If something in this document turns out to be impossible or clearly wrong with the chosen stack, say so in one or two sentences, propose the closest alternative, and proceed with it.
- Do not add features beyond this document without asking. Do not remove features because they are hard; flag them and continue with the rest.
- Keep the audio thread real-time safe at all times. Treat any allocation, lock, or I/O on the audio thread as a bug.
