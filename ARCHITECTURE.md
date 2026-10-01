# Reamp Rig architecture

Current through phase 6 (polish, version 0.1.0). Everything described here is implemented.

## Modules

| Module          | Depends on                | Contents |
|-----------------|---------------------------|----------|
| `Source/Engine` | JUCE core/events/audio modules only | `AudioDeviceInterface`, `JuceAudioDevice`, `DeviceSession`, `DuplexEngine` (audition, take, meters), `RecordStream` (record FIFO), `Take`, `FileWriter` (writer thread), `Resampler`, `SourceLoader`, `LoopbackTestDevice`, `SyncMeasurer` + `SyncMeasurement` (sync key and stored value) |
| `Source/Model`  | JUCE core/events/audio formats | `FileItem` (status, warnings, output), `FileTree`, `FolderScanner`, `BatchQueue`, `OutputNaming` |
| `Source/UI`     | JUCE GUI, Model, Engine API | Theme tokens, embedded fonts, `ForgeLookAndFeel`, the view components, `ConfirmDialog` (themed in-window confirmation) |
| `Source/App`    | everything                | JUCE application, main window, `Settings`, `AudioController` (device/audition glue), `BatchController` (batch glue), `BatchLog` (sidecar log), `OutputOptions` (DESTINATION/OPTIONS binding), `SyncController` (SYNC section, measurement runs), `SyncPlan` (which measurement a batch needs and uses), `MicrophonePermission`, command line (`--open`, dev flags), snapshot (dev aid) |

The Engine has **no dependency on the GUI**. The UI never reaches into engine internals; it reads
a thread-safe state snapshot and receives change notifications on the message thread.

## Threads (all implemented)

1. **Audio thread** (device callback only). One duplex callback on one device
   (`engine::DuplexCallback`, implemented by `DuplexEngine`):
   - phase 3: zeroes every output channel, writes the preloaded audition source (selected
     channel, gain applied, ramped over one block when the gain changes) to the chosen output
     channel, and measures the selected input and output channels (peak + clip atomics);
   - phase 4: the take (source from sample 0 into the output channel, the input channel into
     the take's lock-free `RecordStream`, exactly `recordLength` samples) and callback-gap
     detection during a take.
   No allocation, locks, logging or file access in our code. (JUCE's `AudioDeviceManager`
   wraps the callback in its own `audioCallbackLock`, which is only contended while the
   message thread adds/removes callbacks, i.e. when the device is reconfigured.)
2. **Writer thread** (`FileWriter`, "Take writer", phase 4): polls the take's `RecordStream`
   every 2 ms (the audio thread never signals), drops the first `latency` samples, resamples
   back to the file rate when the device ran at another rate, writes exactly
   `sourceLength + tail` samples to a hidden temp file in the destination folder and renames it
   on completion. It also feeds every written block to the waveform panel's recorded thumbnail.
   The sidecar log is written by the message thread between takes (small appends).
3. **Loader threads** (`SourceLoader`, "Source loader"): decode the played channel of a file
   into memory, measure its peak and resample it to the device rate when they differ (the
   high-quality `Resampler`). Results reach the message thread through
   `MessageManager::callAsync`; a newer request on the same loader cancels the older one. There
   are two instances: the AudioController's (audition preview of the lead file) and the
   BatchController's (the current file of the batch, and the next file preloaded while the
   current one records). Phase 2 already has two background threads of this kind: the **folder
   scanner** thread and the **thumbnail** thread owned by `juce::AudioThumbnailCache` (the
   recorded lane has its own one-entry cache for a finished take read from disk).
4. **Sync analysis thread** (`SyncMeasurer`'s worker, "Sync analysis", phase 5): lives for one
   measurement; drains each repeat's `RecordStream` into memory (polling every 2 ms, like the
   writer) and runs the FFT cross-correlation. No file is written.
5. **Message thread**: UI, settings persistence, device configuration, batch control. Polls the
   engine snapshot (`DuplexEngine::poll()`) at 30 Hz for meters, playhead, take progress and
   status; device state arrives as `DeviceStatus` copies plus `AudioDeviceInterface::Listener`
   notifications.

## Data flow for one take (phase 4, implemented)

```
 loader thread              audio thread (DuplexEngine::process)        writer thread (FileWriter)
 -------------              ------------------------------------        --------------------------
 SourceLoader: decode  ──▶  out[sel] = source[pos] * gain               RecordStream.read
 played channel, resample   (pos < sourceLength, else silence)          skip first `latency` samples
 to the device rate         RecordStream.write (in[sel])  ──FIFO──▶      (resample device -> file rate)
 (immutable LoadedSource)   for exactly recordLength samples            write sourceLength + tail
                            position, gaps, dropped -> atomics           temp file -> rename
                                     │                                  onWritten -> recorded thumbnail
                                     ▼
 message thread: Take::update (engine snapshot) -> stopTake when captured -> writer result -> warnings
```

- `engine::Take` (message thread) builds one take: it sizes a `RecordStream` (4 s of the device
  rate, allocated here, never on the audio thread), starts the `FileWriter` job, then hands the
  source and the stream to `DuplexEngine::startTake` as one immutable command through the same
  atomic pointer as audition (`Command`, `retire()`, `callbackCount`). Audition and take share the
  slot, so starting a take ends an audition. `update()` stops the engine's take once every sample
  is captured and collects the writer's result; `cancel()` stops both and deletes the temp file.
- **Latency math (PROMPT.md 4.3).** Output starts at sample 0 of the source in the same callback
  in which capture starts, so input sample *k* of the take holds what the round trip returned
  from output sample *k - latency*. The engine captures `recordLength = sourceLength + latency +
  tail` samples; the writer drops the first `latency` and writes the remaining `sourceLength +
  tail`. With tail 0 the file is exactly as long as the source and sample-aligned with it. With a
  unity loop at 0 dB it is bit-exact (the loopback test proves it for buffers 64/256/480/1024 and
  delays from 0 to 3000 samples). `latency` is the round trip in device samples, supplied by the
  caller: the BatchController uses the sync measurement stored for exactly the configuration the
  take runs in (see "Sync measurement"), else the driver-reported input + output latency as an
  estimate, and then marks the file "not calibrated" (status badge NC, sidecar log).
- **Exact PCM.** JUCE's `writeFromFloatArrays` scales by 2^31 - 1 and would turn a 24-bit value
  `k` into `k - 1` above -6 dBFS. The writer converts itself (`round (x * 2^(bits-1))`, clamped,
  left-justified), the exact inverse of how JUCE reads PCM, and writes 32-bit as IEEE float.
- **Dropouts.** The audio thread counts samples the FIFO could not take (overflow) and callback
  gaps (a callback more than 1.75 buffers, and at least 3 ms more than one buffer, after the
  previous one, timed with the high-resolution clock). The device's own xrun count
  (`DeviceStatus::xrunCount`: `AudioDeviceManager::getXRunCount()`, i.e. the driver count where
  one exists plus JUCE's callback-overrun count; CoreAudio has no native count) is compared before
  and after each take. Any of these marks the file with a dropout warning (XR); samples lost to an
  overflow are padded with silence at the end so the length stays exact.
- **Level checks.** The writer measures the peak of what it writes: below -60 dBFS is "recorded
  silence?" (SIL), at or above 0.9999 is "clipped" (CLIP). Warnings only.
- **Temp file and rename.** `.<name>.reamprig-part.wav` (hidden) in the destination folder,
  renamed on success (`overwrite` replaces an existing file; otherwise a file that appeared
  meanwhile is not replaced and the take is reported as an error). Cancel, pause, stop, skip and
  quitting delete it.

## Sample rate per file and the resampler (phase 4, implemented)

- **Grouping (PROMPT.md 4.4).** The batch keeps list order (3.3.1) and switches the device rate
  only when the next file's rate differs from the device's, so a run of consecutive files with
  the same rate (a group, `BatchQueue::getGroups`) costs at most one reopen. Before each file the
  BatchController reopens the device at the file's rate through `AudioController::switchSampleRate`
  (not saved) if the device lists that rate; the original rate is restored when the batch ends.
  The driver latency estimate is read again after every switch.
- **Fallback.** If the device cannot run at the file's rate, the loader resamples the source to
  the device rate for playback, the engine records at the device rate, and the writer resamples
  the recording back to the file rate, so the file still has the source's rate and length
  ("resampled" warning, RS). The capture is longer by the resampler's half width so the last
  output samples see real input.
- **`engine::Resampler`.** Kaiser-windowed sinc (64 zero crossings per side, beta 10, cutoff at
  0.47 of the lower rate, kernel tabulated at 2048 points per zero crossing with linear
  interpolation), evaluated at the exact position of every output sample: output sample *m* is
  the input at time *m / outputRate*, so sample 0 maps to sample 0, with no latency and no
  fractional delay left to compensate. Positions use exact integer arithmetic for integer rates
  (no drift over long files). `ResamplerStream` (writer thread) produces the same samples as the
  offline `process()` for any block sizes (tested bit-identical). **Measured error** on a faded
  1 kHz sine at -6 dBFS (`ResamplerTests`): 44.1 -> 48 kHz -122.2 dBFS, 48 -> 44.1 kHz
  -122.6 dBFS, 44.1 -> 96 kHz -121.9 dBFS, 96 -> 48 kHz -126.4 dBFS (peak deviation from the
  ideal sine), round trip 44.1 -> 48 -> 44.1 kHz -116.4 dBFS; through the whole take path
  (44.1 kHz file, device fixed at 48 kHz, loopback) -118.5 dBFS. The requirement is better than
  -80 dB; JUCE's interpolator used in phase 3 reached about -40 dB and is no longer used. Passband
  to about 0.447 of the lower rate (19.7 kHz at 44.1 kHz). Material with energy above that (hard
  cuts, as in the synthetic test files) loses it; that is band-limiting, not misalignment.

## Sync measurement (phase 5, implemented)

```
 SyncController (message thread) ── start ──▶ SyncMeasurer ── startTake (LoadedSource = test signal) ──▶ DuplexEngine
   SYNC section, chip, Settings               │  update (engine snapshot, 30 Hz)                         audio thread: the normal
                                               ▼                                                           take path, nothing new
                                     worker thread "Sync analysis": RecordStream ─▶ recording ─▶ FFT correlation ─▶ SyncRepeat
                                               │  5 repeats ─▶ combine (outliers, median, confidence) ─▶ SyncResult
                                               ▼
                         Settings keyed store (only on success) ─▶ BatchController looks up each take's configuration
```

- **Path.** The measurement uses the batch's own engine path: `DuplexEngine::startTake` with an
  in-memory `LoadedSource` holding the test signal at the device rate and a `RecordStream` big
  enough for the whole repeat. Output starts at signal sample 0 in the callback where capture
  starts, so the lag at which the signal comes back is exactly the `latency` a take discards. The
  audio thread does nothing it does not already do for a take. The engine gain is set to 0 dB for
  the measurement (the level is absolute) and restored afterwards; audition is stopped and the
  AUDIO controls and Start are locked meanwhile.
- **Test signal** (`makeTestSignal`): a one-sample click at the level, 5 ms of silence, then a
  50 ms exponential sine sweep from 200 Hz to min(20 kHz, 0.45 × rate), Hann-faded over 2 ms at
  both ends. Peak = the sync level (default -12 dBFS, -60..0 dBFS, persisted). About 2650 samples
  at 48 kHz. The sweep carries almost all of the correlation energy; the click is the audible
  marker the spec asks for.
- **Recording**: 1 s per repeat, longer when the driver-reported latency needs it
  (`max (1 s, signal + 2 × driver estimate + 0.1 s)`), so round trips up to about 0.94 s are found
  by default.
- **Correlation** (`analyse`): `corr[k] = sum recording[i + k] · signal[i]` for every lag with the
  whole signal inside the recording (0 .. n - m), computed with `juce::dsp::FFT` (real-only
  transforms, zero-padded to the next power of two ≥ n + m, `X · conj (S)`, inverse). The round trip
  is the lag of the largest **|corr|** (a polarity flip still gives the right lag; it is reported).
  Integer samples only, exact: a pure delay with gain has its autocorrelation maximum at exactly
  that lag.
- **Per-repeat checks and thresholds** (constants in `SyncMeasurer.h`):

  | check | threshold | outcome |
  |---|---|---|
  | any recorded sample ≥ 0.9999 | `DuplexEngine::clipLevel` | clipped (the measurement stops at once) |
  | recording peak below -90 dBFS | `silenceDb` | nothing came back |
  | peak-to-sidelobe ratio below 8 dB | `minPeakToSidelobeDb` | no clear peak |
  | returned peak (largest sample where the signal came back) below -60 dBFS | `minReturnedDb` | level too low |
  | samples lost to the FIFO or a callback gap | | dropout |

  Peak-to-sidelobe ratio = |corr| at the peak over the largest |corr| more than 2 periods of the
  sweep's start frequency (10 ms, 480 samples at 48 kHz) away from it. A clean loop gives 43.7 dB;
  pure noise 0-1 dB; the built-in speakers + microphone about 16-17 dB.
- **Combination** (`combine`): 5 repeats; the run stops early when clipped or when three repeats
  have failed (five can no longer give three good ones). Of the good repeats, those more than
  **±1 sample** from their median are outliers; at least **3** must agree, else "not repeatable"
  (the delays are listed). Result = the median of the agreeing repeats; returned peak and
  peak-to-sidelobe ratio are their medians. **Confidence**: *high* = all 5 agree and ratio ≥ 20 dB;
  *medium* = at least 4 agree and ratio ≥ 12 dB; *low* otherwise (stored, shown in `warn`).
  With fewer than 3 good repeats the most frequent failure is reported: nothing came back, no
  clear peak, level too low, dropouts. Failures, a cancel and a stalled device (no progress for
  2 s) never store anything; an older measurement for that configuration stays.
- **Keyed store** (`Settings`, `engine::SyncKey` / `engine::SyncMeasurement`): key = driver type +
  input device + output device + sample rate + buffer size (channels are not part of it); value =
  samples, ms (derived from samples and rate when read), returned peak dBFS, peak-to-sidelobe dB,
  repeats used / total, confidence, date. Stored as one XML value `syncMeasurements`
  (`<SYNC><MEASUREMENT type input output rate buffer samples ms peakDb psrDb used total confidence
  date/>…</SYNC>`). Reading validates every entry (numbers, a key that makes sense, 0 < samples ≤
  10 s, a known confidence word) and ignores the ones that do not; an unreadable date leaves the
  entry valid with an unknown date. A new measurement replaces the old one for its key only.
- **Use** (`SyncPlan`, `BatchController`): every take looks up the measurement for the
  configuration it runs in (`takeStatus`; the batch switches the rate per file group, so each rate
  is its own key). With one: `latencyMeasured = true`, no NC. Without: the driver estimate and NC.
  Before Start, the current configuration and each rate the queued files will switch the device to
  (`deviceRateFor`: the file's rate if the device offers it, else the current one) are checked;
  missing ones are listed in the themed "Not synced for this configuration" dialog (Start anyway /
  Cancel). `--batch-check` counts as confirmed. The sidecar log's Latency header has one line per
  configuration (measured with value, confidence and date, or estimated) and every file entry says
  which latency it used.
- **Forget** (phase 6): a secondary button beside Sync, enabled when the current configuration has
  a measurement and nothing measures or records. It shows the themed `ui::ConfirmDialog` ("Forget
  the sync measurement?", the key and value); confirmed, `Settings::removeSyncMeasurement` deletes
  that key only. A device that stops during a measurement ends it at once
  (`AudioController::onSyncDeviceStopped`): nothing is stored, the SYNC section shows the failure.
- **Display** (`AudioController::refreshSyncUi`, after every device, rate or buffer change and after
  a measurement): the top-bar chip shows `1234 SMP · 25.7 MS` in `ok` when the current configuration
  has a measurement, `NOT SYNCED` in `warn` otherwise (`NO DEVICE` muted when closed); the SYNC
  section shows measured value, returned peak, confidence (ratio and agreement in its tooltip),
  date, and the driver's `in N + out M smp` next to it for reference.

## Batch (phase 4, implemented)

```
 TopBar Start/Pause/Skip/Stop ─▶ BatchController ─▶ BatchQueue (order, state)     OutputNaming (target, collisions)
 FileTreeView menu/header    ─▶ (message thread)  ─▶ AudioController (switchSampleRate, engine, checkCanRecord)
                                       │          ─▶ SourceLoader (own instance: current + next) ─▶ Take ─▶ FileWriter
                                       ▼
            FileTree.setResult/setProgress, WaveformPanel (playhead, recorded lane), StatusBar line, BatchLog
```

- Start refuses without an open input (the same message as everywhere, e.g. microphone access
  denied), with unusable destination options, or with nothing queued. It stops audition and
  locks the AUDIO, DESTINATION and OPTIONS controls (the meters stay live) for the run.
- Per file: resolve the output (`OutputNaming::resolve`, collision policy: Skip marks the file
  Skipped), switch the rate if needed, take the preloaded source or load it, run the take,
  then preload the next file at its predicted device rate. Progress reaches the list through
  `FileTree::setProgress` (repaints one row) about every 0.4 %, the playhead and the status line
  "File 7 of 23 — 00:12 / 01:03 — ETA 14:20" at 30 Hz. ETA = audio still to record (remaining
  files + tail + latency each) divided by the pace measured so far (audio finished / active wall
  time, so loading, switching and writing count), real time before the first file finishes.
- Pause discards the current take; that file is Queued again and is redone from its start on
  Resume (a re-amp cannot be resumed mid-file without a discontinuity). Skip marks it Skipped.
  Stop discards it and leaves it Queued. A device that stops pauses the batch. A file removed
  from the list during the batch is skipped.
- **Pause between files (phase 6).** `pauseBetweenFilesSeconds` (OPTIONS, 0..60 s, default 2) is
  read at Start. When a take finishes (`takeDone`), `settleUntilMs = now + pause`. `next()`
  advances the queue; if the deadline is still ahead the BatchController enters `Phase::waiting`
  and runs a `juce::Timer` (100 ms, message thread, never a sleep) that updates the status line
  ("File 4 of 7 — next in 2 s — ETA 00:35") and calls `beginFile()` once the deadline has passed.
  The upcoming file is highlighted as current meanwhile, the last take stays in the waveform
  panel. Pause during the wait pauses before that file; Resume always waits the pause once more
  (the interrupted take or a device that just came back may still ring). Skip during the wait
  skips the upcoming file and keeps the deadline for the next one. Stop ends the batch. A sync
  measurement cannot start while a batch is active; the timer checks `isSyncActive()` anyway
  before recording. The first take of a batch starts at once.
- **ETA** = audio still to record (remaining files + tail + latency each) divided by the pace
  measured so far, **plus** the waits still to come: the rest of the current wait and one pause
  per unfinished file after the current one (`BatchQueue::countUnfinishedAfterCurrent`). Paused
  time and the waits are excluded from the pace, so nothing is counted twice.
- **Write failures (phase 6).** `FileWriter` checks every `AudioFormatWriter::write` result and,
  after closing the file, that the data actually landed on disk (size check: a full disk can
  show only when the buffer is flushed); a refused write, a failed rename or an unwritable
  destination sets `WriteResult/TakeResult::writeFailed` and deletes the temp file. The
  BatchController then pauses the batch when the volume is full (free space below the file's size
  + 1 MB) or the destination is the single output folder (every later file would fail too), with
  a status-bar error that says what to do (free space and Resume; check the folder or Stop and
  choose another). An unwritable subfolder next to one source only marks that file Error.
- **NC redo.** The BatchController remembers the `SyncKey` of every NC take (session only, like
  the list). "Redo files with warnings" (`getRedoableIds`) re-queues XR/SIL/CLIP files and NC
  files whose key has a measurement by now; the file list asks it for the header button and the
  context menu count, refreshed when a measurement is stored or forgotten.
- Sidecar log (`BatchLog`): one plain-text file per batch, "Reamp Rig batch <date> <time>.txt",
  in the destination folder of the first file handled (the output folder itself in single-folder
  mode). Header: device, rate, buffer, channels, level, latency source, format, tail, naming,
  destination, collision policy. One entry per file as it finishes (source -> output, channel,
  samples and rates, device rate and buffer, gain, latency used and whether measured or
  estimated, peak, dropout details, warnings), then a footer. Appended as the batch goes, so an
  interrupted batch leaves a complete record of what was done.

## Audio device layer (phase 3, implemented)

```
 Settings ──▶ AudioController ──▶ DeviceSession ──▶ AudioDeviceInterface ◀── JuceAudioDevice (CoreAudio/ASIO/WASAPI)
 (App)        (App, message        (Engine: resolve,    (Engine, abstract)     LoopbackTestDevice (Engine: tests, --virtual-device)
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
  chip and the SYNC readouts (stored measurement or "Not synced", phase 5) and the driver latency
  in the SYNC section.
- **Device loss and reconnection (phase 6).** A device that stops or disappears (listener
  notification, or the 2 s poll in the AudioController's timer as a fallback) stops audition,
  pauses a running batch (`onDeviceStopped`; the take is discarded, the file stays queued) and
  ends a running Sync (`onSyncDeviceStopped`), each with a status-bar message that says the
  device reopens by itself. The controller keeps the *preferred* configuration: the saved one
  at launch (or what a first launch opened) and every choice the user makes. On every device
  list change and every 2 s, `DeviceSession::isPresent (preferred)` and
  `DeviceSession::runs (status, preferred)` feed an `engine::ReconnectWatch`: when the preferred
  devices were missing (unplugged, or absent at launch so a fallback opened) and are listed
  again, the controller reopens them with `applyConfig (preferred, persist = false)`, unless a
  batch is running (then it waits until the batch ends) or Sync measures. A paused batch says
  "<device> is back. Press Resume" (`onDeviceReopened`); it never resumes by itself.
  `LoopbackTestDevice::setPresent` simulates unplugging for `--virtual-unplug`.
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
  the time readout to the playhead (accent) and pages the view along when zoomed in; the batch
  uses it too. All AUDIO controls are stock JUCE widgets drawn entirely by
  `ForgeLookAndFeel` (combos, popups, slider); `juce::AudioDeviceSelectorComponent` is not used.

- Phase 6: the app icon (`Assets/Icon/make_icon.py` writes PNGs at 16-1024 px: black square,
  the accent mark of the top-bar logo; `ICON_BIG`/`ICON_SMALL` in `juce_add_gui_app`, JUCE builds
  `AppIcon.icns` and sets `CFBundleIconFile`). Tooltips on every control: JUCE's `ScrollBar` is
  not a tooltip client, so the waveform scrollbar is a `TooltipScrollBar` and the list and
  sidebar use `TooltipViewport`; `setSliderTooltip` also sets the slider's value box (JUCE
  copies the tooltip only when the box is created). `setTextEditorEnabled` shows a locked text
  field's text in `muted` (JUCE keeps the colour). Sidebar rows take fixed-width fields
  (Sync | Forget). Collapsing a folder group deselects its files (`FileTree::deselect`), and
  cmd-A selects the visible files only. The confirmation dialog swallows every key except
  Return / Escape and command shortcuts (cmd-Q still quits).

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
  The batch uses `setStatus`, `setResult` (status, progress, warnings, output file, note) and
  `setProgress` (notifies `Listener::fileProgressChanged`, so views repaint one row).
  `getIdsWithWarnings(mask)` feeds "Redo files with warnings"; `resetStatus` clears warnings too.
- `FileItem` (phase 4): `root` (the folder or file the user added that brought it in, set by the
  scanner; used to mirror folders), `warnings` (`Warning` flags: notCalibrated NC, resampled RS,
  dropout XR, silence SIL, clipped CLIP; `Warning::redoable` = XR, SIL, CLIP), `outputFile`,
  `note` (why it was skipped or failed).
- `BatchQueue` (phase 4): the Queued files of the list in list order at Start (Done, Skipped
  and Error files are left out until reset; files added during a run are not picked up),
  `advance` / `finishCurrent` / `pause` (keeps the current entry for a redo) / `resume` / `stop`
  / `remove`, `peekNext` for preloading, sample-rate groups of consecutive entries and the
  remaining seconds for the ETA.
- `OutputNaming` (phase 4): `<prefix><name><suffix>[_L|_R].wav`, legal file names, destination
  folder (subfolder next to the source, or the single output folder, flat or mirrored below the
  parent of the added folder, so adding "Session A" gives `<out>/Session A/Takes/...`), collision
  policies (overwrite, skip, auto-number `name (2).wav`), validation messages and the example
  line.
- `FolderScanner`: `scan(inputs, recursive, formats, shouldAbort, outputSubfolderName)` is
  synchronous and used by the tests. While recursing, a subfolder named like the DESTINATION
  subfolder (default "Reamped", case-insensitive where the file system is) is not entered and is
  returned in `skippedOutputFolders` (status bar: "Skipped Reamped (output folder)"); a folder
  added directly is always scanned. `scanAsync(...)` runs it on the scanner's own single-thread `juce::ThreadPool`
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

## Simulated devices

- **`engine::LoopbackTestDevice`** (PROMPT.md 4.5): the selected output channel is cabled back
  into the selected input channel. The output block of one callback is "played" while the next
  is processed, as on real hardware, so output stream position *m* reaches the input at
  *m + bufferSize + delay* (round trip `getRoundTripSamples()`), times `gain`, plus optional
  uniform noise. A delay shorter than the buffer is therefore modelled as on real hardware (one
  buffer plus a few samples), never as an impossible zero-latency loop. It reports driver
  latencies that add up to the round trip (overridable to simulate a wrong estimate), other
  inputs carry 0.25 and outputs are pre-filled with 0.5 so wrong routing or unzeroed outputs
  fail the tests. `render()` drives it synchronously (tests); with `paced` it runs its own
  real-time thread (optionally faster than real time). `streamTime()`, `addTimeGap()` and
  `simulateXrun()` let tests provoke dropouts.
- `--virtual-device` is this device paced in real time with the loop off (silent inputs), named
  "Virtual Interface" (it replaces phase 3's `VirtualAudioDevice`); `--virtual-loopback=<n>`
  turns the loop on, `--virtual-rates` restricts its rates (to force resampling) and
  `--virtual-speed` runs it faster than real time, `--virtual-reported-latency=<n>` makes its
  driver latency wrong on purpose. `--batch-check` and `--sync-check` use it.

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
scroll or the scrollbar pans.

Recorded lane (phase 4, PROMPT.md 3.5.2): a second `AudioThumbnail` holds the result of the
current or last take only. During a take the writer thread adds every block it writes
(`WaveformPanel::addRecordedSamples`, serialised with resets by a mutex; the thumbnail locks its
own data for drawing), so the lane grows behind the playhead, already latency-compensated and on
the source's time axis. Selecting a Done file after the batch shows its take built from the
written file (on a one-entry cache's thread). While a batch runs the panel follows the batch's
current file instead of the selection.

## Settings (phases 2-4, implemented)

`rf::app::Settings` owns a `juce::PropertiesFile` (XML file
`~/Library/Application Support/Reamp Rig/Reamp Rig.settings` on macOS; tests pass their own
file). It is owned by the application object and passed to the main window. Keys so far:
`includeSubfolders` (default on); `deviceType`, `inputDevice`, `outputDevice`, `sampleRate`,
`bufferSize`, `inputChannel` + `inputChannelName`, `outputChannel` + `outputChannelName`
(empty/0/-1 = not chosen), `outputGainDb` (-60..+12, default 0, clamped on read and write);
phase 4: `tailMs` (0..60000, default 0), `prefix` (""), `suffix` ("_reamp"), `destinationMode`
(`subfolder` default / `singleFolder`), `subfolderName` ("Reamped"; empty reads as the default),
`outputFolder` (absolute path or empty), `mirrorStructure` (on), `channelTag` (off), `bitDepth`
(16 / 24 default / 32 float), `collisionPolicy` (`autoNumber` default / `overwrite` / `skip`);
phase 5: `syncLevelDb` (-60..0, default -12) and `syncMeasurements` (the keyed sync store, see
"Sync measurement"); phase 6: `pauseBetweenFilesSeconds` (0..60 s in tenths, default 2) and
`windowBounds` ("x y w h" of the window's outer frame, screen coordinates).

**Window state (phase 6).** `MainWindow` saves its frame (content bounds plus the native title
bar, `ComponentPeer::getFrameSizeIfPresent`) on every move and resize and when it closes, but not
while full screen or minimised. On launch the saved frame goes through `clampWindowBounds`
(pure, unit tested): the display it overlaps most (else the main display, centred), at least
1100 x 700 content, at most the display's usable area, moved inside it with the title bar below
the menu bar.
`App/OutputOptions` binds the DESTINATION and OPTIONS controls to these keys (saved on every
change). `--settings-file=<path>` points the app at another file (development checks).
Hand-edited garbage falls back to safe values. The file list is never persisted.

## Tests

JUCE `UnitTest`, not Catch2: it is already part of `juce_core`, needs no extra download or
dependency, and the code under test uses JUCE types throughout. `Tests/TestMain.cpp` is a JUCE
console app (`ReampRigTests`) that runs every test or one category
(`--category=<name>`) and exits non-zero on any failure. CMake registers one ctest entry per
category (`FolderScanner`, `FileTree`, `FileTreeView`, `Settings`, `DeviceSession`,
`DuplexEngine`, `SourceLoader`, `Resampler`, `Loopback`, `Take`, `OutputNaming`, `BatchQueue`,
`Sync`, `Keyboard`). `Keyboard` drives the confirmation dialog and the file list headlessly
(keys in every state, collapsed groups via a synthetic mouse event). `Tests/LoopbackRig.h` holds the loopback device setup, test files and the take `Rig`
shared by `Loopback` and `Sync`.
`Loopback` is the end-to-end engine test (files on disk -> SourceLoader -> DuplexEngine ->
LoopbackTestDevice -> RecordStream -> FileWriter -> file on disk, compared sample by sample). Device logic runs against `Tests/FakeAudioDevice.h`, a
scriptable `AudioDeviceInterface` (types, devices, named channels, rates, buffer sizes, failing
devices) whose `render()` drives the callback block by block and captures every output channel;
it passes *all* channels to the callback, so the tests also prove unused outputs are zeroed.
Engine sources without hardware dependencies are compiled into the tests; `JuceAudioDevice` is
app-only. Model sources are compiled into both the
app and the test executable (JUCE modules are compiled per target, so a shared static library
would duplicate them). Filesystem tests create a fresh temporary directory with small WAV files
written by `juce::WavAudioFormat` and delete it afterwards. `FileTreeView` keyboard handling is
tested headlessly by sending `KeyPress`es to the list component.
