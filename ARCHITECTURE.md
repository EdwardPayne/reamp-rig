# Reamp Forge architecture

Phase 1 outline. Sections marked *planned* describe the design from PROMPT.md section 4 that later
phases implement; they will be expanded (including the sync math) as the code lands.

## Modules

| Module          | Depends on                | Contents |
|-----------------|---------------------------|----------|
| `Source/Engine` | JUCE audio modules only   | *planned:* `AudioDeviceInterface`, `DuplexEngine`, `Take`, `SyncMeasurer`, `Resampler`, `FileWriter`, `LoopbackTestDevice` |
| `Source/Model`  | JUCE core/audio formats   | *planned:* `FileItem`, `FileTree`, `BatchQueue`, `OutputNaming`, `FolderScanner` |
| `Source/UI`     | JUCE GUI, Model, Engine API | Theme tokens, embedded fonts, `ForgeLookAndFeel`, the view components |
| `Source/App`    | everything                | JUCE application, main window, settings, keyboard commands |

The Engine has **no dependency on the GUI**. The UI never reaches into engine internals; it reads
a thread-safe state snapshot and receives change notifications on the message thread.

## Threads (planned)

1. **Audio thread** (device callback only). One duplex `AudioIODeviceCallback` on one device:
   - writes the next block of the preloaded source (selected channel, gain applied) to the chosen
     output channel and zeroes all other outputs;
   - pushes the chosen input channel into a lock-free record FIFO;
   - updates atomics for meters, position and xrun detection.
   No allocation, locks, logging or file access. Ever.
2. **Writer thread**: drains the record FIFO, drops the first `latency` samples, writes
   `sourceLength + tail` samples to a temp file, renames it on completion, writes the sidecar log.
3. **Loader thread**: decodes the upcoming file(s) into memory ahead of time (next file preloaded
   while the current one records), resamples when the device cannot run at the file's rate,
   builds waveform thumbnails.
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
