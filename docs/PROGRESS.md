# Progress log

Single source of truth for where the project stands. Update at the end of every phase.
Phases are defined in `PROMPT.md` section 8; requirements are numbered per `PROMPT.md`.

## Status

| Phase | Scope | Status | Verified | Snapshot |
|---|---|---|---|---|
| 1 | Skeleton + theme | **Done** | 2026-09-30, clean Release build, launched, window captured | `docs/phase1.png` |
| 2 | Files + waveform | **Done** | 2026-09-30, clean Release build, 3/3 ctest entries pass, launched with `--open`, snapshots checked | `docs/phase2.png`, `docs/phase2-zoom.png` |
| 3 | Audio device layer | **Done** | 2026-09-30, clean Release build, 7/7 ctest entries pass (59 cases, 323 checks), launched on real CoreAudio devices, `--audition-check` passed on the virtual device, snapshots checked | `docs/phase3.png`, `docs/phase3-audition.png` |
| 4 | Engine + batch | **Done** | 2026-10-01, clean Release build, 12/12 ctest entries pass (164 cases, 1095 checks), `--batch-check` passed on the virtual loopback device (rate switching, forced resampling, pause/resume/skip/stop), outputs compared with a script, a quiet real-hardware batch passed, launched on real CoreAudio devices, snapshot checked | `docs/phase4.png` |
| 5 | Sync | **Done** | 2026-10-01, clean Release build, 13/13 ctest entries pass (196 cases, 1382 checks), `--sync-check` exact on the virtual loopback at delays 37/300/1500, sync then `--batch-check` in one process: 7/7 Done without NC and bit-exact against a deliberately wrong driver estimate, acoustic sync on the built-in speakers + mic passed (medium confidence), launched on real CoreAudio devices, snapshots checked | `docs/phase5.png`, `docs/phase5-dialog.png` |
| 6 | Polish | Not started | | |

Environment used so far: macOS 26.6 (Apple Silicon), CMake 3.27.8, Apple Clang 21, Xcode
Command Line Tools only, no Ninja. JUCE 9.0.3 fetched by CMake. Clean build about 1 minute.
Terminal now has microphone access on the development Mac (the phase 4 hardware check recorded).

## Next up: phase 6 — polish

Spec: `PROMPT.md` section 8 phase 6 ("tooltips, keyboard shortcuts, error states, dropout
detection, xrun redo, window state, final pass on visuals against the reference site") and the
rules in section 5. Deliver, in this order:

1. **Owner requests (2026-10-01):**
   - **Pause between files** in OPTIONS, seconds, default **2 s**, persisted (`Settings`, typed
     accessor + round-trip test). `BatchController::next()` waits that long after a file is
     written before the next take starts (a message-thread timer, not a sleep); the status line
     shows the wait ("File 4 of 7 — next in 2 s"); Pause/Stop/Skip work during the wait; the ETA
     counts it.
   - **Never scan the output subfolder as a source**: with "Include subfolders" on, skip any
     subfolder whose name equals the configured subfolder name (default "Reamped"), and say so in
     the status bar ("Skipped Reamped (output folder)"). `FolderScanner::scan` gets the name;
     extend `FolderScannerTests`.
2. **App icon**: black square with the accent mark like the top-bar logo, `ICON_BIG`/`ICON_SMALL`
   in `juce_add_gui_app` (`CFBundleIconFile`), so Finder and the Dock no longer show a generic
   icon.
3. **Window size and position remembered** (`PROMPT.md` 3.7 and section 5): save on move/resize
   and quit, restore on launch, clamp to a visible display and the 1100×700 minimum.
4. **Tooltips everywhere**: audit every interactive control (all of them should have one; check
   the SYNC level slider, the dialog buttons, the file list header buttons, the waveform
   scrollbar, the meters) and make texts consistent.
5. **Keyboard shortcuts** (section 5): Space = audition selected / stop, Delete = remove
   selected, cmd/ctrl-A = select all, L / R = set channel on selection. They exist; verify them
   with the confirmation dialog open (it takes Escape/Return and swallows the rest), while Sync
   measures and while a batch runs, and document them (README + tooltips).
6. **Error states**: device disappears during a take or a sync (the sync stops after 2 s without
   progress; the batch pauses), disk full / unwritable destination mid-batch, microphone denied,
   no output channel; every case a plain-language status-bar message with the right tone and a
   recovery path. Re-open the device by itself when it comes back (open issue below).
7. **Dropout detection and xrun redo**: exists since phase 4 (XR badge, "Redo files with
   warnings"); review the gap thresholds on real hardware and decide with the owner whether NC
   files should be redoable once their configuration is synced (they are not today).
8. **Final visual pass** against https://plugins.omarchy.org and section 5 (spacing on the 8 px
   grid, label tracking, disabled states, the dialog, the new SYNC rows), with snapshots
   `docs/phase6*.png`.
9. The rest of "Open issues" below, each closed or re-filed with a reason:
   - **manual mouse pass** with the owner (phases 2-5 list: clicks, L/R, context menus, drag and
     drop, zoom, the AUDIO/DESTINATION/OPTIONS/SYNC controls, the transport, the dialog buttons);
   - **first cabled sync on the Apollo** (expect high confidence) and a batch with it; repeat the
     acoustic built-in check once to see how much the split pair drifts between sessions;
   - **device comes back**: reopen the saved device by itself when it reappears;
   - decide with the owner: a "measure all rates" action, deleting stored measurements, NC files
     in "Redo files with warnings", the thumbnail resolution at maximum zoom, hidden selected
     files in collapsed groups;
   - keep as known limits (document in the README if still true): band-limiting of resampled
     takes, memory per loaded file (current + next, audition preview), gap detection on a loaded
     machine, Windows untested (ASIO/WASAPI, type switching, sync), the stale Command Line Tools
     headers on this Mac (owner decides), microphone prompts attributed to Terminal.

Hooks left by phase 5:

- `SyncController::isBusy()` / `AudioController::isSyncActive()` tell whether a measurement owns
  the device; anything new that touches the engine (the pause timer) must respect it and
  `isBatchActive()`.
- `ui::ConfirmDialog` is the app's only modal; reuse it for any other confirmation (it is a
  child of `MainComponent`, so snapshots capture it).
- `--settings-file` isolates every check from the owner's settings; keep using it.
- `--press-start`, `--sync-check[-rates|-hardware]`, `--sync-level`, `--virtual-reported-latency`
  are in `Source/App/CommandLine.h`.

## Phase 5 — what was done (2026-10-01)

- `Source/Engine/`: **`SyncMeasurer`** (PROMPT.md 3.6): test signal = one-sample click, 5 ms gap,
  50 ms exponential sweep 200 Hz to min(20 kHz, 0.45 fs), 2 ms Hann fades, peak at the sync level;
  each repeat runs through `DuplexEngine::startTake` with an in-memory `LoadedSource` and a
  `RecordStream` (no FileWriter, no `engine::Take`) at an engine gain of 0 dB (restored), records
  1 s (longer if the driver estimate needs it), and its own worker thread ("Sync analysis") drains
  the stream and cross-correlates with `juce::dsp::FFT`; peak of |corr| = round trip in samples.
  5 repeats, outliers > ±1 sample from the median discarded, at least 3 must agree, median of the
  rest; returned peak, peak-to-sidelobe ratio, confidence high/medium/low; failures in plain
  language (nothing came back, no clear peak, level too low, clipped, not repeatable, dropouts,
  device stopped, cancelled), never stored. Algorithm and thresholds in `ARCHITECTURE.md`
  ("Sync measurement"). **`SyncMeasurement.h`**: `SyncKey` (type + input + output device + rate +
  buffer) and `SyncMeasurement` (samples, ms, returned peak, ratio, repeats, confidence, date).
  The audio thread is untouched.
- **Keyed store** in `Settings` (`getSyncMeasurement` / `setSyncMeasurement` /
  `getSyncMeasurements`, one XML value, entry-by-entry validation) and `syncLevelDb` (-60..0,
  default -12).
- **Use in the batch**: `SyncPlan` (pure: `deviceRateFor`, `keysForBatch`, `missing`,
  `chooseLatency`). `BatchController::startTake` looks up the configuration of *that* take
  (`takeStatus`, so each rate group uses its own key): measured → `latencyMeasured`, no NC;
  otherwise the driver estimate and NC. Per-file log line says "measured: … ms, confidence …,
  synced <date>; driver reports …" or "estimated: … not calibrated"; the log header has one Latency
  line per configuration of the batch. **Start warning**: before a batch, the current
  configuration and every rate the queued files switch to are checked; missing ones are listed in
  the themed **`ui::ConfirmDialog`** "Not synced for this configuration" (Start anyway / Cancel,
  Return / Escape) and in the status bar. `--batch-check` treats it as confirmed and prints which
  configurations are synced.
- **UI**: SYNC section: hint, **Sync level** slider (dBFS, persisted, double-click -12), Measured
  (`556 smp · 11.6 ms`, ok colour; "not synced" in warn), Returned peak, Confidence (high ok /
  medium / low warn; ratio and agreement in the tooltip), Measured on (date), Driver
  (`in N + out M smp`, tooltip with the sum and the difference to the measurement), a failure line
  (warn/error badge, full message in the tooltip), five repeat cells while measuring, Sync button
  (Stop while measuring). **Top-bar chip**: measured value in ok, NOT SYNCED in warn, NO DEVICE
  muted; refreshed after every device, rate or buffer change (`AudioController::refreshSyncUi`).
  While Sync measures: audition stopped, AUDIO controls and Start locked (`setSyncActive`,
  `TopBar::setStartAllowed`); Sync is disabled while a batch or audition runs.
- `App/SyncController` (SYNC section glue, runs, stores, status-bar messages, `--sync-check`).
- Dev flags: `--sync-check`, `--sync-check-hardware`, `--sync-check-rates=`, `--sync-level=`,
  `--virtual-reported-latency=`, `--settings-file=`, `--press-start` (`CommandLine.h`).
- Tests: new category **`Sync`** (27 cases): signal shape and level; analysis exact at delays
  1…40000 and with inverted polarity, rejects silence, pure noise, a constant and clipping;
  combination (median, outlier → medium, 3 of 5 or weak ratio → low, unstable/clipped/silent/too
  low/dropout failures); **LoopbackTestDevice** at 44.1/48/96 kHz, buffers 64…1024, round trips
  101, 256, 812, 1029, 1480, 2565, 3128 and 60256 (driver hint lengthens the recording), exact and
  high confidence; noise ±0.02 exact; loop gain -12 dB at level -6 exact; output level ignored and
  restored; one dropout discarded (4/5, medium); cancel; clipped return rejected after one repeat;
  silent return rejected after three; -78 dBFS return rejected as too low; noise-only return → no
  clear peak; **wrong driver estimate** (claims 30, loop is 293): the measurement is stored, the
  batch's choice picks it, the take is bit-exact, and with the estimate the take is the source
  263 samples late (shown exactly); **SyncPlan** per-rate keys and lookups. `Settings`: sync level
  default/clamp/garbage, five keys (rates, buffers, split devices, non-ASCII), overwrite, invalid
  keys refused, garbage XML and ten broken entries ignored one by one. Loopback helpers moved to
  `Tests/LoopbackRig.h`. 196 test cases, 1382 checks.

Verification commands (repository root; `SP` = the session scratchpad with the phase 2 audio):

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"   # no errors, no warnings from our code
ctest --test-dir build --output-on-failure                       # 13/13 passed (196 cases, 1382 checks)
APP="build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
# 1. Sync on the virtual loopback, three delays (isolated settings files): exit 0 each
"$APP" --settings-file="$SP/p5/delay37.settings" --virtual-loopback=37 --output-channel=3 --input-channel=2 --sync-check
#    5 x 293 smp, peak-to-sidelobe 43.7 dB, returned -12.0 dBFS; OK 293 smp · 6.10 ms, high; true round trip 293 -> exact
#    --virtual-loopback=300: 556 smp · 11.6 ms exact; --virtual-loopback=1500: 1756 smp · 36.6 ms exact
# 2. Sync at 48/44.1/96 kHz, then the batch in the same process, driver estimate wrong on purpose (100):
"$APP" --settings-file="$SP/p5/batch.settings" --open="$SP/audio/Session A" --open="$SP/audio/Session B" \
       --output-level=0 --output-channel=3 --input-channel=2 --virtual-loopback=300 --virtual-reported-latency=100 \
       --virtual-speed=8 --sync-check --sync-check-rates=44100,96000 --batch-check="$SP/p5/out1"
#    3 x 556 smp exact; 7/7 Done, no NC, "latency 556 smp measured", 6 bit-exact + Take 01 (32f source)
#    -144.5 dBFS = 24-bit rounding; exit 0. Same batch without a measurement: 5/5 NC, "latency 100 smp
#    estimated", content check FAIL (-1.9 to +0.5 dBFS error): the estimate misaligns, the measurement fixes it
# 3. Acoustic, built-in speakers + microphone, sync level -30 dBFS (system volume 63 %):
"$APP" --settings-file="$SP/p5/hw.settings" --device-type=CoreAudio --output-device="MacBook Pro Speakers" \
       --input-device="MacBook Pro Microphone" --sync-check --sync-check-hardware --sync-level=-30
#    repeats 3425, 3425, 3424, 3425, 3434 (inverted) smp; peak-to-sidelobe 16.2-17.2 dB; returned -17 to -20 dBFS
#    OK 3425 smp · 71.4 ms, medium (4/5 within ±1); driver estimate 4828 smp; exit 0
# 4. Snapshots
"$APP" --settings-file="$SP/p5/snap.settings" --open="$SP/audio/Session A" --open="$SP/audio/Session B" --output-level=0 \
       --output-channel=3 --input-channel=2 --virtual-loopback=300 --virtual-reported-latency=100 --virtual-speed=2 \
       --sync-check --sync-check-rates=44100,96000 --batch-check="$SP/p5/out3" --sidebar-scroll=sync \
       --snapshot="$PWD/docs/phase5.png"
cp "$SP/p5/delay300.settings" "$SP/p5/dialog.settings"      # only 48 kHz measured
"$APP" --settings-file="$SP/p5/dialog.settings" --open="$SP/audio/Session A" --open="$SP/audio/Session B" \
       --virtual-loopback=300 --output-channel=3 --input-channel=2 --select="Riff 01.wav" --press-start \
       --snapshot="$PWD/docs/phase5-dialog.png"             # lists 96 kHz and 44.1 kHz
# 5. Real app, no flags, 6 s, quit via AppleScript: exit 0, devices listed, no errors
```

The owner's settings file was not touched by any check (all ran with `--settings-file`). The
acoustic check played five 57 ms bursts at -30 dBFS through the speakers; nothing else was played.

Deviations from the spec (phase 5):

1. **The confirmation is an in-window overlay** (`ui::ConfirmDialog`, a child of the main
   component with a dimmed backdrop), not a separate window: JUCE's `AlertWindow`/`DialogWindow`
   are stock-styled desktop windows with an OS shadow. Return = Start anyway, Escape = Cancel.
2. **"Prominent warning next to Start"** is the top-bar chip (NOT SYNCED in warn, directly left
   of Start) plus the Start dialog and a warn status-bar line; no extra label was added.
3. **Low-confidence measurements are stored** (shown in warn, status bar suggests measuring
   again); only failures are refused. "Not repeatable" (fewer than 3 of 5 within ±1 sample) is a
   failure.
4. The run **stops early**: at the first clipped repeat, or after three failed repeats.
5. **Polarity**: the peak of |correlation| is used, so an inverting loop still measures; the
   inversion is reported, not treated as a failure.
6. **Integer samples** only (no sub-sample interpolation): the take discards whole samples.
7. During Sync the **output level is not applied** (engine gain 0 dB, restored): the sync level
   is absolute dBFS. Range -60..0 dBFS.
8. **Stop** during a measurement (the Sync button turns into Stop); nothing is stored.
9. The key has no channels (as the spec's key); every output/input channel pair of a device
   shares its measurement.
10. The BatchController's per-rate lookup is unit-tested through the pure `SyncPlan` functions it
    uses (BatchController itself needs the whole UI); end to end it is proven in the app by
    `--sync-check --sync-check-rates` + `--batch-check` (three rates, every file measured).
11. JUCE module `juce_dsp` added (for `juce::dsp::FFT`; part of JUCE, no new dependency).
12. More development flags (list above), including `--settings-file`.

## Phase 4 — what was done (2026-10-01)

- `Source/Engine/`: **take mode in `DuplexEngine`** beside audition through the same immutable
  command and atomic pointer (`startTake (source, recordLength, stream)`, `stopTake`): source from
  sample 0 into the output channel with gain, the input channel into the take's lock-free
  **`RecordStream`** (`juce::AbstractFifo` over a buffer allocated on the message thread; counts
  dropped samples, callback gaps and position), all other outputs zeroed, exactly
  `sourceLength + latency + tail` samples captured; callback-gap detection (> 1.75 buffers and
  > buffer + 3 ms, injectable clock for tests); `EngineSnapshot` carries the take's progress, gaps
  and overflow. **`FileWriter`** (writer thread): drains the FIFO, discards the latency, resamples
  back to the file rate if needed, writes WAV 16/24/32f at the source rate with an exact
  float-to-PCM conversion, hidden temp file renamed on completion, pads samples lost to an
  overflow, measures the peak, feeds the recorded thumbnail. **`Take`**: one take's lengths,
  FIFO, writer job and engine command; silence (< -60 dBFS) and clip (>= 0.9999) checks.
  **`Resampler`** + `ResamplerStream`: Kaiser-windowed sinc evaluated at exact positions (no
  latency, no fractional offset), -122 dBFS on a test sine, -116 dBFS round trip (details and all
  numbers in `ARCHITECTURE.md`); the `SourceLoader` now uses it too. **`LoopbackTestDevice`**:
  output fed back to input after one buffer + `delay`, gain, noise, exact reported latency, wrong
  channels detectable, `render()` for tests and a paced thread for the app.
  `DeviceStatus::xrunCount` (from `AudioDeviceManager::getXRunCount()`).
- `Source/Model/`: **`BatchQueue`** (Queued files in list order, done files left out until reset,
  advance/finish/pause/resume/stop/remove, peekNext, rate groups, remaining seconds for the ETA),
  **`OutputNaming`** (prefix/suffix, channel tag, legal names, subfolder next to source or a
  single folder flat or mirrored below the parent of the added folder, collision policies
  overwrite/skip/auto-number "name (2).wav", validation, example line). `FileItem` gained `root`
  (set by the scanner), `warnings`, `outputFile`, `note`; `FileTree` gained `setResult`,
  `setProgress` (+ `Listener::fileProgressChanged`) and `getIdsWithWarnings`.
- UI: **DESTINATION** live (square radio buttons for the mode, subfolder name or output-folder
  field with a native folder chooser and the mirror checkbox, prefix, suffix, live example line
  that follows the selected file, format, collision policy) and **OPTIONS** (channel tag, tail in
  ms), all persisted on every change (`App/OutputOptions`). **Top bar** Start / Pause-Resume /
  Skip / Stop wired. **File list**: status with warning badges (NC, RS, XR, SIL, CLIP; tooltip
  spells them out with the output path), per-file progress (Done bars in `ok`), the current file
  highlighted (accent tint and edge), context menu "Redo files with warnings", "Skip current
  file", "Reset status" (never the file being recorded), and a "Redo warnings" header button when
  such files exist. **Waveform panel**: batch playhead on the current file and the RECORDED lane
  growing live under the source on the same axis (writer thread -> thumbnail); a Done file shows
  its take from disk when selected. **Status line** "File 4 of 7 — 00:02 / 00:06 — ETA 00:35".
- App: **`BatchController`** (sample-rate switching per file with restore at the end, resampled
  fallback, own `SourceLoader` preloading the next file, driver-latency estimate marked "not
  calibrated", refuses to start without an open input with the same status message, pauses when
  the device stops, skips files removed meanwhile), **`BatchLog`** (sidecar log, see
  `ARCHITECTURE.md`), `AudioController` hooks (engine access, `checkCanRecord`,
  `switchSampleRate`, `restoreConfig`, `setBatchActive` locks AUDIO and stops audition).
- Persistence: `tailMs`, `prefix`, `suffix`, `destinationMode`, `subfolderName`, `outputFolder`,
  `mirrorStructure`, `channelTag`, `bitDepth`, `collisionPolicy`, typed accessors and round-trip
  tests (defaults, every key, every enum value, non-ASCII, limits, garbage).
- Dev flags: `--virtual-device` is now the `LoopbackTestDevice` (paced, silent; replaces
  `App/VirtualAudioDevice`), `--virtual-loopback[=n]`, `--virtual-rates=`, `--virtual-speed=`,
  `--batch-check=<folder>`, `--batch-check-transport`, `--batch-check-hardware`,
  `--sidebar-scroll=<section>` (`Source/App/CommandLine.h`).
- Tests (new categories `Resampler`, `Loopback`, `Take`, `OutputNaming`, `BatchQueue`; `Settings`
  extended): **end-to-end loopback** (files on disk through loader, engine, loopback device, FIFO
  and writer back to disk): mono, stereo L and stereo R × buffers 64/256/480/1024 × delays
  0/1/37/256/1000 at tail 0, exact length and bit-exact at 0 dB with a unity loop (60 takes);
  gain (-6 dB × loop 0.7, 32f) within 1e-6; tail 25 ms (length + 1200, source part exact, tail
  silent); buffer 64 with delay 3000; 16-bit and 32-bit float sources exact in their own format;
  44.1 kHz source on a device fixed at 48 kHz: exact length, -118.5 dBFS. A deliberate off-by-one
  latency fails 60 checks (mutation check). `Take`: FIFO overflow counted, writer pads to the
  exact length, callback gaps detected only during a take and not for jitter, gap -> dropout,
  xrun count, silence and clip, cancel leaves no file, unwritable destination fails before
  playback, audition/take command slot, record lengths. Resampler: lengths, exact positions,
  four rate pairs and a round trip below -80 dB with alignment proven, streaming bit-identical to
  offline. 164 test cases, 1095 checks.

Verification commands (repository root; `SP` = the session scratchpad with the phase 2 audio):

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"   # no errors, no warnings from our code
ctest --test-dir build --output-on-failure                       # 12/12 passed
APP="build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
# 1. Batch on the virtual loopback (rates 44.1/48/96, device switches per file): exit 0
"$APP" --open="$SP/audio/Session A" --open="$SP/audio/Session B" --output-level=0 --output-channel=3 \
       --input-channel=2 --virtual-speed=8 --batch-check="$SP/p4/out1"
#    7/7 Done, every output exactly as long as its source, 6 bit-exact with the played channel,
#    Take 01 (32-bit float source, 24-bit output) -144.5 dBFS = 24-bit rounding; sidecar log written
# 2. Device fixed at 48 kHz (resampling), buffer 256, loop delay 37: exit 0
"$APP" ... --buffer-size=256 --virtual-loopback=37 --virtual-rates=48000 --batch-check="$SP/p4/out2"
#    44.1/96 kHz files "resampled", exact lengths, -144.5 dBFS against the offline
#    file -> 48 kHz -> file reference (i.e. aligned and identical up to 24-bit rounding)
# 3. Pause/Resume, Skip, Stop exercised in the running app: exit 0
"$APP" ... --virtual-speed=4 --batch-check="$SP/p4/out4" --batch-check-transport
# 4. Snapshot mid-batch with DESTINATION in view
"$APP" ... --buffer-size=256 --virtual-speed=2 --sidebar-scroll=destination \
       --batch-check="$SP/p4/out3" --snapshot="$PWD/docs/phase4.png"
# 5. Independent comparison (pure Python, reads the WAV integers)
python3 "$SP/p4/compare.py" "$SP/audio/Session A/Riff 01.wav" "$SP/p4/out1/Session A/Riff 01_reamp.wav" 0
#    length equal: True; samples differing: 0 of 576000; max abs difference: 0
# 6. Real hardware, quiet (2 s, 440 Hz at -12 dBFS, output level -24 dB), built-in speakers + mic:
"$APP" --open="$SP/p4/hw/Quiet Test.wav" --device-type=CoreAudio --output-device="MacBook Pro Speakers" \
       --input-device="MacBook Pro Microphone" --output-level=-24 --batch-check-hardware --batch-check="$SP/p4/out_hw"
#    Done, 96000 samples (= source), latency estimate in 3482 + out 1346, exit 0
# 7. Real app, no flags, 6 s, quit via AppleScript: exit 0, devices listed, no errors
```

Deviations from the spec (phase 4):

1. **Pause discards the current take**: the file goes back to Queued and is recorded again from
   its start on Resume (a re-amp cannot continue mid-file without a discontinuity). Stop also
   discards it and leaves it Queued; Skip marks it Skipped.
2. **Loopback model**: the `LoopbackTestDevice` round trip is one buffer plus the configured delay
   (output written in one callback is heard from the next, as on hardware). A loop that returns
   a sample in the same callback is not causal, so "delay 0/1/37" means 64+0, 64+1, ... for buffer
   64. The tests cover round trips that are not multiples of the buffer, shorter and longer than
   it.
3. `App/VirtualAudioDevice` was replaced by the `LoopbackTestDevice` (same names, paced, silent
   by default); more dev flags (list above).
4. **Skip** is a fourth top-bar button (after Pause); "Redo files with warnings" is a context
   menu item and a header button that only appears when such files exist.
5. "Redo files with warnings" re-queues files with dropout, silence or clip warnings. NC and RS
   do not count: a new take would get them again (phase 5 can add NC files once calibrated).
6. While a batch runs the AUDIO controls (except the meters), DESTINATION, OPTIONS and audition
   are locked; the waveform panel follows the batch instead of the selection.
7. **Sample-rate groups** are runs of consecutive files in list order (3.3.1 keeps list order); the
   list is not reordered by rate. The device's rate is restored after the batch.
8. Files added or reset while a batch runs are not picked up by that run.
9. **ETA** is the remaining time (m:ss), not a clock time, at the pace measured so far.
10. **Sidecar log**: one per batch, in the destination folder of the first file handled (the
    output folder itself in single-folder mode).
11. **Mirror structure** is relative to the parent of the folder the user added (so the added
    folder's own name is kept); needs `FileItem::root`, set by the scanner.
12. Default suffix "_reamp" (the phase 1 placeholder; the spec names no default).
13. **Own float-to-PCM conversion** in the writer: JUCE 9's `writeFromFloatArrays` scales by
    2^31 - 1 and writes 24-bit `k` as `k - 1` above -6 dBFS (16-bit above -6 dBFS too), which
    breaks bit-exactness.
14. **Xruns**: CoreAudio has no native xrun count; the app uses `AudioDeviceManager::getXRunCount()`
    (JUCE's callback-overrun count plus the driver's where it exists) and its own callback-gap
    detection. A gap shorter than ~0.75 buffer extra is not detected at small buffers.
15. Radio buttons are drawn as squares with a filled inner square (no round shapes anywhere).
16. The audition preview now uses the new Resampler as well (one resampler for both paths).
17. The status column is wider (152 px) to fit the warning badges; the name column gives way.

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
APP="build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
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
APP="build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
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
- App "Reamp Rig", bundle id `com.reamprig.app`, min window 1100×700, ad-hoc signed as a
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

- **Owner requests for phase 6 (2026-10-01):**
  1. A **pause between files** setting in OPTIONS, in seconds, default **2 s**, persisted.
     The batch waits that long after a file is written before the next take starts, so amp
     and reverb tails die out. Show the wait in the status line.
  2. **Never scan the output subfolder as a source.** When a folder is added with subfolders
     on, skip any subfolder whose name equals the configured output subfolder name
     (default "Reamped"), and report the skip in the status bar.
  The default suffix `_reamp` is approved as is.
- **No app icon.** The bundle has no `CFBundleIconFile`, so Finder and the Dock show a generic
  icon and the app looks the same as the raw executable inside `Contents/MacOS`. The owner
  launched that inner binary once by mistake and got a Terminal window. Phase 6: add an icon
  set (black square, accent mark, matching the top-bar logo) via `ICON_BIG`/`ICON_SMALL` in
  `juce_add_gui_app`.
- **Microphone prompts (resolved for Terminal).** Phase 3 left two prompts for **Terminal**
  (apps launched from a shell are attributed to their responsible process). By 2026-10-01
  Terminal has access: the phase 4 hardware run recorded without a prompt. Launched from Finder
  (`open …app`) the app is asked under its own name. `tccutil reset Microphone
  com.apple.Terminal` undoes a decision.
- Phase 3, verified manually by the owner on real hardware on 2026-10-01: files load,
  waveforms render, the input meter moves with a live signal, real devices show in the pickers,
  and output audition plays through the speakers with the playhead tracking. **Audition and the
  input meter are now verified.** Mouse interactions (below) are still unverified.
- **Mouse interactions not exercised automatically.** Click, shift/cmd-click, clicking L/R,
  the context menu, collapsing groups, drag-and-drop from Finder, scroll/pinch zoom, dragging the
  marker and the Add dialogs could not be driven: this Mac does not allow synthetic input
  (`osascript` keystrokes refused, error 1002) or screen capture. Phase 3 adds: the AUDIO combos
  and their popups, the level slider, clicking a meter to clear CLIP, the Audition button and
  Space (window-level key handler; tested only through code review). Needs a manual pass.
  Phase 4 adds: the DESTINATION radios, the output-folder chooser, the text fields and combos,
  the channel-tag and tail fields, clicking Start / Pause / Resume / Skip / Stop, the new context
  menu items and the "Redo warnings" button, hovering status cells for the warning tooltip. The
  batch itself, including pause/resume/skip/stop, was driven programmatically
  (`--batch-check`, `--batch-check-transport`), not by clicks. Phase 5 adds: the Sync button and
  Stop, the sync level slider, the Start anyway / Cancel buttons and Return/Escape in the dialog,
  hovering the SYNC readouts and the chip for their tooltips. Sync and the dialog were driven
  through `--sync-check` and `--press-start`, not by clicks.
- **Phase 4 on real hardware**: one quiet 2 s batch on the built-in speakers + microphone
  (exact length, sidecar log, NC warning). The take recorded a peak of -5.8 dBFS from a -36 dBFS
  output: room/microphone level, not checked further.
- **Sync on real hardware (phase 5)**: only acoustically, built-in speakers -> room -> built-in
  microphone at -30 dBFS: 3425 smp (71.4 ms), medium confidence (4 of 5 within ±1; the fifth 3434
  and inverted, a reflection), peak-to-sidelobe 16-17 dB, while the driver reports 3482 + 1346 =
  4828 smp. So the CoreAudio estimate for this split pair is about 29 ms too long (it includes
  safety offsets / the aggregate's buffering, not checked further). Split devices drift, so this
  value is not stable across sessions; not measured twice. **No cabled interface loop (Apollo)
  measured yet**: that is the first real test of Sync and of the high-confidence path on hardware.
- Sync with ASIO / WASAPI never tested (Windows).
- **No "measure all rates" action**: Sync measures the current configuration; the user switches the
  rate and presses Sync again for each rate the files use (the Start dialog lists what is
  missing). `--sync-check-rates` does it for checks. Ask the owner whether a button is wanted
  (not in the spec).
- Stored measurements are never pruned and cannot be deleted from the UI (a new measurement
  replaces the old one for its key). Harmless; mention if it matters.
- Files recorded with NC stay NC after the configuration is synced; "Reset status" re-queues
  them ("Redo files with warnings" still excludes NC, see phase 4 deviation 5).
- **No gap between files** (owner request 1 above, phase 6): the next take starts as soon as the
  previous file is written, so an amp/reverb tail of file N can still sound while file N+1 starts.
- **"Subfolder next to source" output is scanned again** (owner request 2 above, phase 6) if the
  user later adds the source folder with "Include subfolders" on.
- Resampled takes lose source content above about 0.447 of the lower rate (band-limiting). The
  synthetic test files have hard note cut-offs, so their resampled results differ from the raw
  source by -20 to -67 dBFS at those edges while matching the offline resampled reference to
  24-bit rounding. Real DI files rarely carry such content.
- The batch holds the played channel of the current and the next file in memory (4 bytes per
  sample each, same limit as the audition preview).
- Gap detection uses wall-clock callback spacing; on a heavily loaded machine with tiny buffers
  scheduling jitter above about 0.75 buffer + 3 ms would be reported as a dropout (warning only).
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

- 2026-10-01: **Sync stays manual per sample rate.** The owner is fine switching the rate and
  pressing Sync for each configuration; no "measure all rates" action will be added.
- 2026-10-01: **Renamed the app from "Reamp Forge" to "Reamp Rig"** (owner choice; "Studio"
  rejected as generic). Product name, bundle id (`com.reamprig.app`), CMake targets
  (`ReampRig`, `ReampRigTests`, artefacts under `build/ReampRig_artefacts`), top-bar logo,
  settings folder, sidecar log name and temp-file suffix all changed. Old settings are copied
  to the new location on first launch. GitHub repo renamed to `EdwardPayne/reamp-rig` (old
  URL redirects). The local working folder is still `reamp-forge`; the `rf::` namespace is
  kept. Older entries in this log keep the old name where they quote it. Snapshots before
  phase 6 still show the old logo; phase 6 re-captures them.
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
- 2026-10-01: Own windowed-sinc `Resampler` for the record path (JUCE's interpolator: about
  -40 dB; ours: about -120 dB); also used for the audition preview.
- 2026-10-01: Pause discards the current take and redoes the file on Resume.
- 2026-10-01: Sync confirmation is an in-window themed overlay (no stock AlertWindow); low-
  confidence measurements are stored, failures never.
