# Reamp Forge architecture

Current through phase 2. Sections marked *planned* describe the design from PROMPT.md section 4
that later phases implement; they will be expanded (including the sync math) as the code lands.

## Modules

| Module          | Depends on                | Contents |
|-----------------|---------------------------|----------|
| `Source/Engine` | JUCE audio modules only   | *planned:* `AudioDeviceInterface`, `DuplexEngine`, `Take`, `SyncMeasurer`, `Resampler`, `FileWriter`, `LoopbackTestDevice` |
| `Source/Model`  | JUCE core/events/audio formats | `FileItem`, `FileTree`, `FolderScanner`; *planned:* `BatchQueue`, `OutputNaming` |
| `Source/UI`     | JUCE GUI, Model, Engine API | Theme tokens, embedded fonts, `ForgeLookAndFeel`, the view components |
| `Source/App`    | everything                | JUCE application, main window, `Settings`, command line (`--open`, dev flags), snapshot aid |

The Engine has **no dependency on the GUI**. The UI never reaches into engine internals; it reads
a thread-safe state snapshot and receives change notifications on the message thread.

## Threads (audio, writer and loader planned; scanner and thumbnail threads implemented)

1. **Audio thread** (device callback only). One duplex `AudioIODeviceCallback` on one device:
   - writes the next block of the preloaded source (selected channel, gain applied) to the chosen
     output channel and zeroes all other outputs;
   - pushes the chosen input channel into a lock-free record FIFO;
   - updates atomics for meters, position and xrun detection.
   No allocation, locks, logging or file access. Ever.
2. **Writer thread**: drains the record FIFO, drops the first `latency` samples, writes
   `sourceLength + tail` samples to a temp file, renames it on completion, writes the sidecar log.
3. **Loader thread** (*planned*): decodes the upcoming file(s) into memory ahead of time (next file
   preloaded while the current one records) and resamples when the device cannot run at the
   file's rate. Phase 2 already has two background threads of this kind (see "Model" below):
   the **folder scanner** thread and the **thumbnail** thread owned by `juce::AudioThumbnailCache`.
4. **Message thread**: UI, settings persistence, device configuration, batch control. Polls the
   engine snapshot on a timer for meters, progress and status.

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

## Settings (phase 2, implemented)

`rf::app::Settings` wraps `juce::ApplicationProperties` (XML file
`~/Library/Application Support/Reamp Forge/Reamp Forge.settings` on macOS). It is owned by the
application object and passed to the main window. Typed accessors are added per phase; phase 2
has `includeSubfolders` (default on). The file list is never persisted.

## Tests

JUCE `UnitTest`, not Catch2: it is already part of `juce_core`, needs no extra download or
dependency, and the code under test uses JUCE types throughout. `Tests/TestMain.cpp` is a JUCE
console app (`ReampForgeTests`) that runs every test or one category
(`--category=<name>`) and exits non-zero on any failure. CMake registers one ctest entry per
category (`FolderScanner`, `FileTree`, `FileTreeView`). Model sources are compiled into both the
app and the test executable (JUCE modules are compiled per target, so a shared static library
would duplicate them). Filesystem tests create a fresh temporary directory with small WAV files
written by `juce::WavAudioFormat` and delete it afterwards. `FileTreeView` keyboard handling is
tested headlessly by sending `KeyPress`es to the list component.
