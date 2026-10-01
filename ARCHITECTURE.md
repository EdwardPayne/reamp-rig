# Reamp Forge architecture

Current through phase 3. Sections marked *planned* describe the design from PROMPT.md section 4
that later phases implement; they will be expanded (including the sync math) as the code lands.

## Modules

| Module          | Depends on                | Contents |
|-----------------|---------------------------|----------|
| `Source/Engine` | JUCE core/events/audio modules only | `AudioDeviceInterface`, `JuceAudioDevice`, `DeviceSession`, `DuplexEngine` (audition + meters), `SourceLoader`; *planned:* `Take`, `SyncMeasurer`, `Resampler`, `FileWriter`, `LoopbackTestDevice` |
| `Source/Model`  | JUCE core/events/audio formats | `FileItem`, `FileTree`, `FolderScanner`; *planned:* `BatchQueue`, `OutputNaming` |
| `Source/UI`     | JUCE GUI, Model, Engine API | Theme tokens, embedded fonts, `ForgeLookAndFeel`, the view components |
| `Source/App`    | everything                | JUCE application, main window, `Settings`, `AudioController` (device/audition glue), `MicrophonePermission`, command line (`--open`, dev flags), `VirtualAudioDevice` and snapshot (dev aids) |

The Engine has **no dependency on the GUI**. The UI never reaches into engine internals; it reads
a thread-safe state snapshot and receives change notifications on the message thread.

## Threads (writer planned; audio, loader, scanner and thumbnail threads implemented)

1. **Audio thread** (device callback only). One duplex callback on one device
   (`engine::DuplexCallback`, implemented by `DuplexEngine`):
   - phase 3: zeroes every output channel, writes the preloaded audition source (selected
     channel, gain applied, ramped over one block when the gain changes) to the chosen output
     channel, and measures the selected input and output channels (peak + clip atomics);
   - phase 4 adds: the take (source from sample 0, input into a lock-free record FIFO) and
     xrun detection.
   No allocation, locks, logging or file access in our code. (JUCE's `AudioDeviceManager`
   wraps the callback in its own `audioCallbackLock`, which is only contended while the
   message thread adds/removes callbacks, i.e. when the device is reconfigured.)
2. **Writer thread** (*planned*): drains the record FIFO, drops the first `latency` samples,
   writes `sourceLength + tail` samples to a temp file, renames it on completion, writes the
   sidecar log.
3. **Loader thread** (`SourceLoader`, "Source loader", phase 3): decodes the played channel of
   the lead file into memory, measures its peak, and resamples it to the device rate when they
   differ (windowed sinc, latency-compensated). Results reach the message thread through
   `MessageManager::callAsync`; a newer request cancels the older one. Phase 4 reuses it to
   preload the next file of the batch. Phase 2 already has two background threads of this kind:
   the **folder scanner** thread and the **thumbnail** thread owned by `juce::AudioThumbnailCache`.
4. **Message thread**: UI, settings persistence, device configuration, batch control. Polls the
   engine snapshot (`DuplexEngine::poll()`) at 30 Hz for meters, playhead and status; device
   state arrives as `DeviceStatus` copies plus `AudioDeviceInterface::Listener` notifications.

## Data flow for one take (planned)

```
 loader thread            audio thread                       writer thread
 -------------            ------------                       -------------
 decode file  --buffer--> output ch <- source * gain
                          input ch  -> record FIFO  --FIFO-->  skip `latency` samples
                                                              write sourceLength + tail
                                                              temp file -> rename
                          atomics (pos, peaks, xruns) --> message thread (UI snapshot)
```

Latency compensation: output starts at sample 0 of the take and capture starts in the same
callback; `sourceLength + latency + tail` samples are recorded and the first `latency` discarded,
so with tail = 0 the result is sample-aligned and exactly as long as the source.

## Audio device layer (phase 3, implemented)

```
 Settings ──▶ AudioController ──▶ DeviceSession ──▶ AudioDeviceInterface ◀── JuceAudioDevice (CoreAudio/ASIO/WASAPI)
 (App)        (App, message        (Engine: resolve,    (Engine, abstract)     VirtualAudioDevice (dev aid, App)
              thread glue)          fallback, open)                            FakeAudioDevice (Tests)
                   │                                          │ setCallback
                   │ startAudition / setGainDb / poll         ▼
                   └────────────────────────────────────▶ DuplexEngine ◀── LoadedSource ◀── SourceLoader
```

- **`AudioDeviceInterface`** (`Engine/AudioDeviceInterface.h`): driver types, device names per
  type and direction, default devices, `open(DeviceConfig)`, `close()`, `getStatus()`
  (`DeviceStatus`: open flag, running config, channel names of both directions, supported rates
  and buffer sizes, driver-reported input/output latency, last error), `setCallback()`, and a
  listener called on the message thread when devices or state change. `DeviceConfig` holds the
  type, input and output device names (the same name for one duplex device), rate, buffer size,
  and one input and one output channel as index **and** driver name. A direction whose channel is
  -1 is not opened at all. The callback gets a `StreamLayout` (rate, buffer, and where the
  selected channels are in the per-callback arrays) before streaming starts.
- **`JuceAudioDevice`** over `juce::AudioDeviceManager`: opens exactly the selected input and
  output channel (JUCE passes only open channels, packed in ascending order, so the layout index
  comes from `packedChannelIndex`). Opening is two steps when the device changes: create it with
  no channels to learn its channel lists, then open the stream with the chosen channels. The
  manager's XML state and default-device fallbacks are never used; if the manager ever starts a
  device other than the requested one (device list change), the callback is not forwarded to it
  and the outputs are zeroed. On macOS JUCE 9 always runs a device through a private aggregate
  device, which also combines a separate input and output device into one duplex callback (with
  drift correction, hence "not sample-synchronized, for testing only" in the UI).
- **`DeviceSession`** (pure logic over the interface, unit tested with a fake device): resolves
  a wanted config against what is present: driver type (else the first), output device (else the
  system default, else the first), input device (else the output device if it has inputs, else
  the default), opens it (sample rate and buffer size fall back to the nearest supported value),
  then matches channels by name, then index, else the first channel, reopening only if the
  channels changed. Every fallback returns a plain-language warning; if the device will not
  open, the system defaults are tried once; nothing throws or crashes when nothing opens.
- **`AudioController`** (App): opens the saved config at startup (dev flags may override it for
  one run) and **never persists at startup**, so a missing Apollo falls back for this session
  only and is used again once it is back. Every user change in the AUDIO section is applied and
  persisted (`Settings::setDeviceConfig`, `setOutputGainDb`). Warnings and errors go to the
  status bar and the log. It fills the AUDIO combos from `DeviceStatus`, shows the split-device
  notice, the top-bar summary (`Apollo Twin · 48 kHz · 256`, "No device" when closed), the sync
  chip ("No device" / "Not synced" until phase 5) and the driver latency in the SYNC section.
- **Microphone permission (macOS, `MicrophonePermission.mm`)**: `AVCaptureDevice`
  authorization status and a non-blocking request. Important finding: because JUCE's aggregate
  device carries *all* streams of its sub-devices, creating any CoreAudio device that has input
  streams (even for output only) makes coreaudiod wait for the microphone prompt and blocks the
  calling thread. So while the status is *undetermined* the controller opens nothing (unless the
  device is output-only and `--no-input` is set), asks, and opens the wanted config when the
  answer arrives. *Denied*: the device opens without input and the status bar explains how to
  allow access in System Settings. Without access CoreAudio would deliver silence anyway.

## Audition path (phase 3, implemented)

```
 lead file (FileTree) ─▶ SourceLoader (loader thread): decode played channel, peak, resample
                              │ callAsync
                              ▼
 AudioController: LoadedSource (immutable, shared_ptr) ── startAudition(source, start) ──▶ DuplexEngine
                                                                                          (atomic Command*)
 audio thread: out[selected] = source[pos..] * gain; other outputs zeroed; pos, peaks → atomics
 message thread (30 Hz): poll() → meters, waveform playhead, "finished" → stop
```

- The lead's played channel is decoded when the lead or its L/R choice changes, and again when
  the device rate changes; its peak drives "Peak at output" (file peak + output level, `warn`
  with "clips" above 0 dBFS).
- `startAudition` publishes an immutable `Command {source, startSample, generation}` through an
  atomic pointer. The audio thread notices a new pointer, jumps to its start and plays; at the
  end it publishes the command's generation as finished. Stopping swaps in null and *retires*
  the command: it is freed only after the callback counter has moved past the value read at
  retirement (or the stream has stopped), so the audio thread never sees freed memory and never
  frees anything itself.
- Space (window-level `keyPressed`, reached when the focused component does not use it) and the
  Audition/Stop button toggle it; buttons do not keep keyboard focus. Changing the lead stops it.

## UI (phase 1, implemented)

- `rf::ui::theme` (`Theme.h`): the design tokens (colours from PROMPT.md section 5) plus metrics.
- `rf::ui::Fonts`: JetBrains Mono and Inter embedded via `juce_add_binary_data`, loaded once and
  shared through `juce::SharedResourcePointer`.
- `rf::ui::ForgeLookAndFeel`: set as the default LookAndFeel at startup; styles buttons (primary /
  secondary via `setButtonStyle`), toggles, combo boxes and popup menus, sliders, text editors,
  labels, scrollbars, tooltips, progress bars and the splitter. It also maps every generic/system
  font request onto the embedded faces so no system font can leak in.
- `rf::app::MainComponent`: top bar, file tree / splitter / waveform stack, 280 px sidebar in a
  scrolling viewport, status bar. The tooltip window is a child of the main component (not a
  desktop window) so it gets no OS drop shadow; popup menus are created without a shadow flag.
  Since phase 2 it also owns the `FileTree` and `FolderScanner`, is the window-wide
  `FileDragAndDropTarget`, and keeps the waveform panel (lead item) and status bar in step with
  the model.
- `rf::ui::FileTreeView` (phase 2): fully custom-painted list (no stock `ListBox`/`TreeView`):
  header with count and Add buttons, column header, folder group rows and file rows with the
  L/R selector, status and progress bar. Selection lives in the model; the view keeps collapse
  state, the shift-click anchor and hover. Shared helpers: `drawChevron`, `drawRightChevron`,
  `drawBadge` (LookAndFeel.h) and `rf::ui::format` (Format.h).
- Phase 3: `rf::ui::LevelMeter` (Meters.h): custom horizontal peak meter, -60..0 dBFS, 24 dB/s
  fall, 1.5 s peak hold (line + readout), a CLIP box that lights in `warn` and latches until the
  meter is clicked. `NoticeLine` (badge + `warn` text) and hideable rows in `SidebarSection`
  (the split-device notice). `WaveformPanel::setPlayhead` draws a 2 px accent playhead, switches
  the time readout to the playhead (accent) and pages the view along when zoomed in; phase 4
  reuses it for the batch. All AUDIO controls are stock JUCE widgets drawn entirely by
  `ForgeLookAndFeel` (combos, popups, slider); `juce::AudioDeviceSelectorComponent` is not used.

## Model (phase 2, implemented)

Namespace `rf::model`. No GUI and no audio-thread code; everything except `FolderScanner::scan`
is message-thread only.

- `FileItem`: one source file: stable `ItemId`, `juce::File`, `AudioFileInfo` (channels, rate,
  bits, float flag, length, format name), `Channel` (L default; only meaningful when the file has
  2+ channels), `FileStatus` (Queued / Recording / Done / Skipped / Error) and `progress` (0..1).
- `FileTree`: the list. Groups keyed by parent folder, in order of first appearance; files
  inside a group in natural file-name order, so the list does not depend on the order files
  arrive. Dedupe by absolute path (case-insensitive where the file system is). Holds the
  selection and the *lead* item (last clicked; shown in the waveform panel), so selection rules
  are testable without a GUI. Rule from PROMPT.md 3.1.6: `setChannel(ids, ch)` changes only items
  with a channel choice; mono items are skipped. Synchronous `Listener::fileTreeChanged()`.
  `setStatus(id, status, progress)` is the hook for the batch in phase 4.
- `FolderScanner`: `scan(inputs, recursive, formats, shouldAbort)` is synchronous and used by
  the tests. `scanAsync(...)` runs it on the scanner's own single-thread `juce::ThreadPool`
  ("Folder scanner") and posts the `ScanResult` to the message thread with
  `MessageManager::callAsync`; a shared `alive` flag drops results that arrive after the scanner
  is destroyed, and scans are delivered in the order queued. Supported formats are whatever
  `AudioFormatManager::registerBasicFormats()` reads. Order: a folder's own files (natural
  order), then its subfolders depth first. Hidden files and symlinked subfolders are skipped
  while recursing. Non-audio extensions inside folders are ignored silently; anything with an
  audio extension that fails to open, any explicitly given non-audio file, missing paths and
  unreadable folders are returned in `skipped` with a reason, which the app shows in the status
  bar (warning tone, full list in its tooltip) and writes to the log.

```
 drop / Add… / --open        scanner thread                message thread
 --------------------        --------------                --------------
 MainComponent::addPaths --> FolderScanner::scan  --callAsync-->  FileTree::add
                             (headers only, no decode)            -> FileTreeView, StatusBar,
                                                                     WaveformPanel (lead item)
```

## Waveform thumbnails (phase 2, implemented)

`WaveformPanel` owns one `juce::AudioThumbnail` (64 source samples per thumbnail sample) and a
`juce::AudioThumbnailCache` of 16 entries. Selecting a file calls `thumbnail.setSource`, which
reads only the file header on the message thread; the level data is generated on the cache's
`TimeSliceThread` and the thumbnail's change messages trigger repaints as data arrives
("Building waveform N%" until complete). Finished thumbnails stay in the cache, so reselecting a
recent file is instant. Stereo files get one lane per channel with the non-selected channel
dimmed; files with more than two channels show their first two. The visible time range drives
both the ruler (tick step chosen from a 1-2-5 series so major ticks are at least 80 px apart) and
the thumbnail drawing; cmd+scroll or pinch zooms around the mouse (minimum 50 ms visible), plain
scroll or the scrollbar pans. Phase 4 adds a second, in-memory thumbnail for the recorded result
(current/last file only, PROMPT.md 3.5.2).

## Settings (phases 2-3, implemented)

`rf::app::Settings` owns a `juce::PropertiesFile` (XML file
`~/Library/Application Support/Reamp Forge/Reamp Forge.settings` on macOS; tests pass their own
file). It is owned by the application object and passed to the main window. Keys so far:
`includeSubfolders` (default on); `deviceType`, `inputDevice`, `outputDevice`, `sampleRate`,
`bufferSize`, `inputChannel` + `inputChannelName`, `outputChannel` + `outputChannelName`
(empty/0/-1 = not chosen), `outputGainDb` (-60..+12, default 0, clamped on read and write).
Hand-edited garbage falls back to safe values. The file list is never persisted.

## Tests

JUCE `UnitTest`, not Catch2: it is already part of `juce_core`, needs no extra download or
dependency, and the code under test uses JUCE types throughout. `Tests/TestMain.cpp` is a JUCE
console app (`ReampForgeTests`) that runs every test or one category
(`--category=<name>`) and exits non-zero on any failure. CMake registers one ctest entry per
category (`FolderScanner`, `FileTree`, `FileTreeView`, `Settings`, `DeviceSession`,
`DuplexEngine`, `SourceLoader`). Device logic runs against `Tests/FakeAudioDevice.h`, a
scriptable `AudioDeviceInterface` (types, devices, named channels, rates, buffer sizes, failing
devices) whose `render()` drives the callback block by block and captures every output channel;
it passes *all* channels to the callback, so the tests also prove unused outputs are zeroed.
Engine sources without hardware dependencies are compiled into the tests; `JuceAudioDevice` is
app-only. Model sources are compiled into both the
app and the test executable (JUCE modules are compiled per target, so a shared static library
would duplicate them). Filesystem tests create a fresh temporary directory with small WAV files
written by `juce::WavAudioFormat` and delete it afterwards. `FileTreeView` keyboard handling is
tested headlessly by sending `KeyPress`es to the list component.
