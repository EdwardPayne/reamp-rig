# Progress log

Single source of truth for where the project stands. Update at the end of every phase.
Phases are defined in `PROMPT.md` section 8; requirements are numbered per `PROMPT.md`.

## Status

**Review fixes (2026-10-01, after phase 6, not committed yet).** All 28 findings of
`docs/REVIEW-2026-10-01.md` are fixed (its Status column says how). Highlights: a take started
across a stream stop/start begins at sample 0 (E1); a stream restart mid-take is XR, or discards
the take and pauses the batch when the rate or buffer changed (E2); a cancelled take never leaves
a file (E3); batch and sync rate switches never fall back to other devices and pause with a
message instead (A1); the app is built for macOS 11 again (U1); one batch never overwrites its
own outputs, names keep `# @ , ;` and long names keep their suffix (U2, U3, U5). Verified: clean
Release build without warnings, 15/15 ctest entries (241 cases, 1745 checks; 214 / 1528 before),
the three engine regressions failed on the old code and pass now, `--batch-check` with per-file
rate switching and a 1 s pause PASS, `--virtual-unplug=4:2` PASS, `--sync-check` exact at delays
37 / 1500 and at 48 / 44.1 / 96 kHz + batch bit-exact, `--virtual-reject-rate=44100` pauses the
batch with the message, a 2 s real batch on the built-in speakers + mic at -24 dB PASS (no XR),
`otool` minos 11.0 and `LSMinimumSystemVersion` 11.0, launched without flags and quit cleanly.

| Phase | Scope | Status | Verified | Snapshot |
|---|---|---|---|---|
| 1 | Skeleton + theme | **Done** | 2026-09-30, clean Release build, launched, window captured | `docs/phase1.png` (removed in phase 6; see `docs/phase6-empty.png`) |
| 2 | Files + waveform | **Done** | 2026-09-30, clean Release build, 3/3 ctest entries pass, launched with `--open`, snapshots checked | removed in phase 6; see `docs/phase6-files.png`, `docs/phase6-zoom.png` |
| 3 | Audio device layer | **Done** | 2026-09-30, clean Release build, 7/7 ctest entries pass (59 cases, 323 checks), launched on real CoreAudio devices, `--audition-check` passed on the virtual device, snapshots checked | removed in phase 6; see `docs/phase6-audio.png`, `docs/phase6-audition.png` |
| 4 | Engine + batch | **Done** | 2026-10-01, clean Release build, 12/12 ctest entries pass (164 cases, 1095 checks), `--batch-check` passed on the virtual loopback device (rate switching, forced resampling, pause/resume/skip/stop), outputs compared with a script, a quiet real-hardware batch passed, launched on real CoreAudio devices, snapshot checked | removed in phase 6; see `docs/phase6-batch.png` |
| 5 | Sync | **Done** | 2026-10-01, clean Release build, 13/13 ctest entries pass (196 cases, 1382 checks), `--sync-check` exact on the virtual loopback at delays 37/300/1500, sync then `--batch-check` in one process: 7/7 Done without NC and bit-exact against a deliberately wrong driver estimate, acoustic sync on the built-in speakers + mic passed (medium confidence), launched on real CoreAudio devices, snapshots checked | removed in phase 6; see `docs/phase6-sync.png`, `docs/phase6-dialog.png` |
| 6 | Polish | **Done** | 2026-10-01, clean Release build (no warnings from our code), 14/14 ctest entries pass (214 cases, 1528 checks), `--batch-check` with a 1 s pause on the virtual loopback (waits and ETA shown, 7/7 exact), unplug/replug, disk-full and unwritable-folder checks, `--sync-check` at three delays and three rates + batch bit-exact, two short real batches and an acoustic sync on the built-in speakers + mic, icon in the bundle, window state restored and clamped, launched without flags and quit cleanly, nine snapshots checked | `docs/phase6-empty.png`, `docs/phase6-files.png`, `docs/phase6-zoom.png`, `docs/phase6-audio.png`, `docs/phase6-audition.png`, `docs/phase6-batch.png`, `docs/phase6-sync.png`, `docs/phase6-dialog.png`, `docs/phase6-forget.png` |

Environment used so far: macOS 26.6 (Apple Silicon), CMake 3.27.8, Apple Clang 21, Xcode
Command Line Tools only, no Ninja. JUCE 9.0.3 fetched by CMake. Clean build about 1 minute.
Terminal now has microphone access on the development Mac (the phase 4 hardware check recorded).

## Next steps after phase 6

All six phases of `PROMPT.md` section 8 are done (version 0.1.0). Candidates, owner's choice:

1. **First Apollo session** (needs the interface). Checklist:
   - Connect the Apollo, launch the app: it should open the saved device by itself if it was
     saved before, else pick it in AUDIO (Driver CoreAudio, Output and Input device "Apollo …",
     the output channel that feeds the amp, the input the amp returns on).
   - Patch a cable from that output straight into that input (no amp), Sync level -12 dBFS,
     press **Sync** at every sample rate the DI files use (switch the rate in AUDIO, Sync again).
     Expect **high** confidence, a round trip of a few hundred samples, and note the difference
     to the driver's figure (SYNC "Driver" tooltip).
   - Re-patch through the amp, set the output level with Audition (Space) and "Peak at output",
     run a short batch (two or three files) with the default 2 s pause; check the files open in
     a DAW sample-aligned with their sources (null test against a DI-only loop if wanted), no NC
     badge, the sidecar log's Latency lines say "measured".
   - Unplug the Apollo during a take once: the batch pauses, the app reopens it by itself when it
     is back, Resume finishes the batch.
   - The manual mouse pass from "Open issues" can be done in the same session.
2. **Windows build**: configure with Visual Studio + CMake, `JUCE_ASIO=1` with Steinberg's ASIO
   SDK on the build machine, check ASIO/WASAPI device names, type switching, Sync and a batch on
   an interface; fix what does not compile (Windows code is isolated in
   `MicrophonePermission.cpp` and the device layer).
3. **Release v0.1.0**: on approval, commit the phase 6 work on `develop`, then
   `git checkout release && git merge --no-ff develop && git tag -a v0.1.0 -m "Reamp Rig 0.1.0"`,
   push `release` and the tag, and keep working on `develop` (`CLAUDE.md`, "Branches"). Bump
   `project(... VERSION ...)` in `CMakeLists.txt` for the next version; it reaches the bundle,
   the sidecar log header and `getApplicationVersion()`.

## Phase 6 — what was done (2026-10-01)

1. **Owner requests.**
   - **Pause between files**: OPTIONS "Pause between files (s)", 0..60 s in tenths, default 2 s,
     persisted (`Settings::get/setPauseBetweenFilesSeconds`, key `pauseBetweenFilesSeconds`,
     round-trip, clamp and garbage tests). `BatchController`: after a take, `next()` waits until
     the deadline on a 100 ms message-thread `juce::Timer` (`Phase::waiting`, no sleep); status
     line "File 2 of 7 — next in 1 s — ETA 00:14" with the upcoming file highlighted; Pause, Skip
     (skips the upcoming file, the deadline stays) and Stop work during the wait; Resume waits
     once more; the ETA adds the rest of the current wait plus one pause per unfinished file after
     the current one (`BatchQueue::countUnfinishedAfterCurrent`, tested), and the pace excludes
     the waits; Sync cannot start while a batch is active and the timer re-checks
     `isSyncActive()` before recording. `--batch-check` prints every wait, its measured length and
     the total.
   - **Output subfolders are never sources**: `FolderScanner::scan/scanAsync` take the DESTINATION
     subfolder name; while recursing, a subfolder with that name (case-insensitive on macOS) is
     not entered and comes back in `ScanResult::skippedOutputFolders`; status bar "Added 7 files.
     Skipped Reamped (output folder)" (the paths in its tooltip and the log). A folder added
     directly is scanned. Tests: nested output folders, a similar name kept, another name, case,
     no name, recursion off, added directly.
2. **App icon**: `Assets/Icon/make_icon.py` (Python 3, no dependencies) writes `icon_16.png` …
   `icon_1024.png`: a black square on the macOS icon grid with a hairline `line` border and the
   accent-orange mark of the top-bar logo in the centre, no text, sharp corners. `ICON_BIG`
   (1024) and `ICON_SMALL` (32) in `juce_add_gui_app`; the bundle has `AppIcon.icns` in
   `Contents/Resources` and `CFBundleIconFile = AppIcon.icns`.
3. **Window size and position**: saved (outer frame, `windowBounds`) on every move/resize and on
   close, restored on launch through `clampWindowBounds` (pure, tested: fits unchanged, second
   display kept, grows to 1100 x 700, shrinks to the display, moved back inside, title bar below
   the menu bar, unplugged display -> centred on the main one, display smaller than the minimum,
   no display). Full screen and minimised are not saved. Logged at launch ("Window: restored …").
   Dev flag `--window-bounds=x,y,w,h`.
4. **Tooltips**: audited every control. Added: the waveform scrollbar (`TooltipScrollBar`), the
   list and sidebar scrollbars (`TooltipViewport`), the slider value boxes (`setSliderTooltip`;
   JUCE copies a slider's tooltip to its text box only when the box is created, so the SYNC level
   and Output level boxes had none), the empty drop zone, Forget (its tooltip says why it is
   off). Reworded for consistency (full sentences, keys in parentheses, "Cmd-scroll"), and they
   mention the new behaviour (Skip during the wait, Resume after the pause, Include subfolders
   skipping output folders, Redo including synced NC files, collapse deselects).
5. **Keyboard shortcuts** verified in every state: new test category `Keyboard` (confirmation
   dialog: Return/Escape, Space/Delete/L/R/arrows swallowed, command shortcuts passed on so cmd-Q
   still quits; list during a batch; collapsed groups via a synthetic click). Space while Sync
   measures or a batch runs is refused by `AudioController::toggleAudition` with a status-bar
   message (code path reviewed). Documented in the README ("Keyboard shortcuts") and the file-row
   tooltip.
6. **Error states**:
   - Device disappears during a take: the batch pauses ("Paused: the audio device stopped. It
     reopens by itself when it is back; then press Resume …"), during a sync the measurement ends
     at once ("Sync stopped: the audio device stopped or was disconnected. Nothing was stored. …").
   - **Reconnection**: the AudioController keeps the preferred (saved or user-chosen)
     configuration and, on every device-list change and a 2 s poll, reopens it when it was missing
     and is listed again (`DeviceSession::isPresent/runs` + `engine::ReconnectWatch`, unit tested
     with the fake device: unplug while open, comeback during a take is deferred, missing at
     launch, picked again by hand). Never under a running batch or a sync; a paused batch then
     says "<device> is back. Press Resume". Checked end to end with `--virtual-unplug=4:2`
     (`LoopbackTestDevice::setPresent`): the batch paused during file 2, the device reopened by
     itself, the check resumed, 5/5 Done and exact.
   - **Disk full / unwritable destination**: `FileWriter` now checks every write and the size on
     disk after closing (a full disk was silently truncating takes before: `write()`'s result was
     ignored), deletes the temp file and flags `writeFailed`; the batch pauses with "Paused: the
     disk is full, so Riff 01_reamp.wav could not be written to … Free some space, then press
     Resume" (checked on a 2 MB disk image) or "Paused: cannot write to <folder>. Check that the
     folder still exists and is writable, then press Resume, or Stop and choose another output
     folder" (checked on a read-only folder); in subfolder mode an unwritable source folder only
     marks that file Error. The sidecar log gets a "Write failed" line.
   - Microphone denied, no device, no output or input channel: every "Cannot start / audition /
     sync" message now ends with what to do ("Pick an output device and channel in AUDIO", …).
7. **Dropout thresholds** reviewed on the built-in speakers + microphone: real batches at buffer
   512 and buffer 64 (two 1 s files each, -24 dB) gave no XR, so the thresholds (gap > 1.75
   buffers and > buffer + 3 ms, i.e. 4.3 ms at 64 samples) stay. **NC redo**: NC files become
   redoable once their configuration has a measurement (`BatchController::getRedoableIds`, the
   take's `SyncKey` is remembered per file for the session); the header button and context menu
   follow the sync store (refreshed after Sync and Forget).
8. **Visual pass** against section 5: disabled text fields now show muted text during a batch
   (they looked editable), the empty RECORDED lane says "Not recorded yet" instead of a stray
   dash, the Forget dialog lists the key and the value on two lines (one was truncated), Sync |
   Forget share a row on the 8 px grid (fixed-width field support in `SidebarSection`), group
   headers show the home folder as "~" (already the case since phase 2; visible in the
   snapshots). Fixed a mis-encoded "±" in the confidence tooltip. Snapshots re-captured with the
   new logo (below); the old `docs/phase1-5*.png` were removed.
9. **SYNC Forget**: a secondary "Forget" beside Sync deletes the current configuration's
   measurement after the themed confirmation (`Settings::removeSyncMeasurement`, tested).
   **Collapsed groups**: collapsing deselects the group's files (`FileTree::deselect`, tested),
   cmd-A selects visible files only.
10. Version 0.1.0 (CMake) reaches the bundle and the sidecar log header ("Reamp Rig 0.1.0 batch
    log"). README: "Using Reamp Rig" (setup, sync, batch, output, warnings glossary, shortcuts),
    "Known limits", new dev flags; developer sections kept. `ARCHITECTURE.md` and
    `Tests/README.md` updated.

Verification commands (repository root; `SP` = the session scratchpad with the test audio,
`D` = `build/demo`, a copy under the home folder so headers show "~"):

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release   # only the known stale-CLT warning
cmake --build build --config Release --clean-first -j"$(sysctl -n hw.ncpu)"   # exit 0, no warnings from our code
ctest --test-dir build --output-on-failure      # 14/14 passed: 214 test cases, 1528 checks
APP="build/ReampRig_artefacts/Release/Reamp Rig.app/Contents/MacOS/Reamp Rig"
# 1. Pause 1 s (settings file with pauseBetweenFilesSeconds = 1.0), virtual loopback:
"$APP" --settings-file="$SP/p6/final.settings" --open="$SP/audio/Session A" --open="$SP/audio/Session B" \
       --output-level=0 --output-channel=3 --input-channel=2 --virtual-speed=8 --batch-check="$SP/p6/out1"
#    wait: File 3 of 7 — next in 1 s — ETA 00:14 / waited 1.03 s before file 3 ... (6 waits)
#    wall time 17.57 s, of which pauses between files 6.15 s; 7/7 Done, exact; PASS, exit 0
# 2. Unplug: same with --open="$SP/audio/Session A" --virtual-speed=4 --virtual-unplug=4:2 -> paused, reopened, resumed, PASS
# 3. Disk full (2 MB HFS+ image): --batch-check=<mount> -> "Paused: the disk is full, so Riff 01_reamp.wav could not
#    be written to …"; read-only folder -> "Paused: cannot write to …"
# 4. Sync regression: --virtual-loopback=37 / 1500 --sync-check -> 293 / 1756 smp exact, high; sync at 48/44.1/96 kHz +
#    batch with a wrong driver estimate (100) -> 556 smp exact at each rate, 7/7 Done "latency 556 smp measured", PASS
# 5. Real hardware, built-in speakers + mic, -24 dB, two 1 s files (440 Hz at -12 dBFS), default 2 s pause:
"$APP" --settings-file="$SP/p6/hw.settings" --open="$SP/p6/hw" --device-type=CoreAudio --output-device="MacBook Pro Speakers" \
       --input-device="MacBook Pro Microphone" --output-level=-24 --batch-check-hardware --batch-check="$SP/p6/out_hw"
#    2/2 Done (48000 samples each, NC), "waited 2.05 s before file 2", PASS; again at --buffer-size=64: no XR
#    Acoustic sync at -30 dBFS: 3431 smp (5/5, low: peak-to-sidelobe 9 dB at system volume 38 %); phase 5 had 3425
# 6. Icon: plutil -p ".../Reamp Rig.app/Contents/Info.plist" -> CFBundleIconFile => "AppIcon.icns";
#    Contents/Resources/AppIcon.icns (6340 bytes)
# 7. Window: --window-bounds=60,100,1300,780 -> saved "60 68 1300 812" (frame); relaunch -> "Window: restored 60 100
#    1300 780"; hand-edited "-5000 -5000 900 500" -> restored 206 173 1100 700 (centred, minimum size)
# 8. Snapshots (all with --settings-file, virtual device unless noted):
#    phase6-empty, phase6-files, phase6-zoom (--view=2.9:3.6), phase6-audio (real speakers + mic, no playback),
#    phase6-audition (--audition-check=6), phase6-batch (--batch-check, --sidebar-scroll=options),
#    phase6-sync (--sync-check, --sidebar-scroll=sync), phase6-dialog (--press-start, only 48 kHz measured),
#    phase6-forget (--press-forget)
# 9. Real app, no flags, 6 s, quit via AppleScript: exit 0
```

Sound played during verification: two 1 s 440 Hz tones per real batch (two batches) at -24 dB
output level and five 57 ms sync bursts at -30 dBFS through the built-in speakers; nothing else.
Every check used `--settings-file` except the final no-flag launch (which only saved the window
position into the owner's settings).

Deviations from the spec / task (phase 6):

1. **Resume waits the pause** between files once more before recording (the interrupted take or a
   device that just came back may still ring); the first file of a batch starts at once.
2. **Skip during the wait** skips the file that was about to start; the deadline stays for the
   next one.
3. **cmd-A selects the visible files** (files in collapsed groups stay unselected), following the
   decision that bulk actions never hit hidden rows.
4. The confirmation dialog passes **command shortcuts** (cmd-Q) on instead of swallowing them.
5. The saved device is **not switched under a running batch** (e.g. one that runs on a fallback
   device); it is reopened when the batch ends. A paused batch never resumes by itself.
6. The pause field has its own row ("Pause between files (s)"); the label does not fit half the
   sidebar width beside "Tail (ms)".
7. Window state is the window's outer frame; full screen / minimised are not remembered.
8. The icon is a script-generated PNG pair turned into `.icns` by JUCE (no Icon Composer file);
   JUCE rescales 1024/32 px for the other sizes, the other PNGs are kept for reference.
9. Thumbnail resolution at maximum zoom kept as a documented known limit (a fix would read
   samples from the file on the message thread or add a second cache: more than an hour).
10. More dev flags: `--window-bounds`, `--press-forget`, `--virtual-unplug`
    (`Source/App/CommandLine.h`).

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

Final list after phase 6 (2026-10-01). Everything else from the phase 5 list was closed (see
"Closed in phase 6" below).

- **Manual mouse pass: done by the owner on 2026-10-01** on the built-in speakers and
  microphone, after phase 6: clicking around the file list, starting batches, syncing and the
  dialogs. Verdict: "Looking really good", no defects reported. Closed; anything found later
  is filed as a normal bug.
- **Needs the Apollo**: the first cabled sync (expect high confidence) and a batch with it,
  including unplugging the interface once during a take (reconnection was only checked on the
  virtual device). Checklist in "Next steps after phase 6". The acoustic built-in check was
  repeated: 3431 smp today vs 3425 in phase 5 (the split pair drifts by a few samples between
  sessions, as expected for separate clocks); the driver's estimate changed from 4828 to 3379 smp
  (CoreAudio reports a different input latency today), confidence low at a lower system volume.
- **Windows untested** (ASIO/WASAPI, driver-type switching where JUCE briefly opens the new
  type's defaults, Sync): known limit, README; "Next steps" item 2.
- **Stale Command Line Tools headers** on the development Mac: `CMakeLists.txt` keeps its
  workaround and warning, README explains the fix (`sudo rm -rf
  /Library/Developer/CommandLineTools/usr/include/c++` or reinstall the CLT). Owner's decision.
- **Known limits, documented in the README**: band-limiting of resampled takes; memory per
  loaded file (current + next, audition preview); gap detection on a heavily loaded machine
  (thresholds reviewed on real hardware at buffers 64 and 512: no false XR); thumbnail
  resolution at maximum zoom; microphone prompts attributed to the terminal when launched from
  one.

Closed in phase 6: pause between files (done); output subfolder scanned again (skipped now); no
app icon (done); window state (done); device comes back (reopened by itself); "measure all rates"
(decided: no); deleting stored measurements (Forget); NC files in "Redo files with warnings"
(redoable once synced); hidden selected files in collapsed groups (deselected); microphone
prompts for Terminal (resolved since phase 4); audition and input meter (verified by the owner);
phase 4 real-hardware note (informational, superseded by the phase 6 runs); sync on real hardware
(re-filed under "Needs the Apollo").

## Decision log

- 2026-10-01: **Review fixes** (`docs/REVIEW-2026-10-01.md`): sample-rate switches of a batch or
  a sync run use DeviceSession's exact mode (never another device; a refusal pauses the batch);
  a device restarted from outside at another rate or buffer size pauses the batch like device
  loss; a cancelled take whose writer had already finished deletes the file (not kept as Done);
  one batch never overwrites its own outputs (the second source is auto-numbered under every
  policy); output names are limited to 240 UTF-8 bytes so the hidden temp name fits 255.
- 2026-10-01: **NC files are redoable once synced**: "Redo files with warnings" includes NC files
  whose device configuration has a measurement by now; the tooltip says so.
- 2026-10-01: **Stored measurements**: a small secondary "Forget" in SYNC deletes the current
  configuration's measurement after the themed confirmation; no bulk management UI.
- 2026-10-01: **Collapsing a folder group deselects its files**, so bulk actions never hit hidden
  rows (cmd-A selects visible files only).
- 2026-10-01: **Thumbnail resolution at maximum zoom** stays a documented known limit.
- 2026-10-01: **Stale Command Line Tools headers** stay the owner's decision; the CMake workaround
  and the README note remain.
- 2026-10-01: **Pause between files** default 2 s, persisted; default suffix `_reamp` kept; the
  output subfolder is never scanned as a source.
- 2026-10-01: A write failure that would repeat (disk full, unwritable single output folder)
  **pauses** the batch; one unwritable source folder only fails its files.
- 2026-10-01: The saved device is **reopened by itself** when it comes back, never under a running
  batch; a paused batch waits for Resume.

- 2026-10-01: **Sync stays manual per sample rate.** The owner is fine switching the rate and
  pressing Sync for each configuration; no "measure all rates" action will be added.
- 2026-10-01: **Renamed the app from "Reamp Forge" to "Reamp Rig"** (owner choice; "Studio"
  rejected as generic). Product name, bundle id (`com.reamprig.app`), CMake targets
  (`ReampRig`, `ReampRigTests`, artefacts under `build/ReampRig_artefacts`), top-bar logo,
  settings folder, sidecar log name and temp-file suffix all changed. Old settings are copied
  to the new location on first launch. GitHub repo renamed to `EdwardPayne/reamp-rig` (old
  URL redirects). The local working folder was renamed to `reamp-rig` the same day; the `rf::`
  namespace is kept. Older entries in this log keep the old name where they quote it. Snapshots before
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
