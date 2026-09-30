# Progress log

Single source of truth for where the project stands. Update at the end of every phase.
Phases are defined in `PROMPT.md` section 8; requirements are numbered per `PROMPT.md`.

## Status

| Phase | Scope | Status | Verified | Snapshot |
|---|---|---|---|---|
| 1 | Skeleton + theme | **Done** | 2026-09-30, clean Release build, launched, window captured | `docs/phase1.png` |
| 2 | Files + waveform | **Done** | 2026-09-30, clean Release build, 3/3 ctest entries pass, launched with `--open`, snapshots checked | `docs/phase2.png`, `docs/phase2-zoom.png` |
| 3 | Audio device layer | Not started | | |
| 4 | Engine + batch | Not started | | |
| 5 | Sync | Not started | | |
| 6 | Polish | Not started | | |

Environment used so far: macOS 26.6 (Apple Silicon), CMake 3.27.8, Apple Clang 21, Xcode
Command Line Tools only, no Ninja. JUCE 9.0.3 fetched by CMake. Clean build about 1 minute.

## Next up: phase 3 — audio device layer

Spec: `PROMPT.md` sections 3.2 and 3.7 (the device/gain keys), 4.1, 4.5 (abstract device
interface, so later tests can use a loopback device), 4.7 (microphone permission), plus the
"Settings round-trip" test from section 7 for the keys added here.

Deliver:

- `Source/Engine/`: `AudioDeviceInterface` (abstract; real implementation over
  `juce::AudioDeviceManager`), no GUI dependency. GUI observes device state through a
  snapshot/listener on the message thread (`ARCHITECTURE.md`).
- Device selection in the AUDIO section: device type (CoreAudio / ASIO / WASAPI), device, sample
  rate, buffer size. On macOS allow separate input and output devices; label that setup
  "not sample-synchronized, for testing only".
- Output and input channel selectors showing the driver's channel names; one mono channel each.
- Live input and output peak meters with a clip indicator that latches until clicked (custom
  component, themed; `warn` for clip).
- Output level -60 to +12 dB, default 0, persisted; show the resulting peak of the current source
  file (needs a peak scan of the lead file off the message thread).
- Audition: play the lead file's selected channel (L/R) through the chosen output, from the
  waveform panel's audition start point, without recording; stoppable any time; Space toggles it.
- Top bar device summary (`Apollo Twin · 48 kHz · 256`) becomes live.
- Persist: device type, input/output devices, sample rate, buffer size, input channel, output
  channel, output gain (tail/naming keys wait for phase 4).
- macOS: request microphone permission on first use of input; clear message when denied.
- Tests: settings round-trip for the new keys; device-interface logic that can run without
  hardware.
- Update this file, `ARCHITECTURE.md`, add `docs/phase3.png`.

Hooks left by phase 2 for phase 3:

- `rf::app::Settings` (`Source/App/Settings.*`): add typed accessors next to
  `get/setIncludeSubfolders`. Owned by the application, passed to `MainWindow`/`MainComponent`.
- The audition target is `fileModel.getLead()` (`model::FileTree`); its `channel` says L or R and
  `info` has rate/length. `MainComponent::fileTreeChanged()` is where lead changes arrive.
- `WaveformPanel::getAuditionStart()` and `onAuditionStartChanged` (seconds). The panel has no
  playhead yet; add one (accent) for audition playback, reuse it for the batch in phase 4.
- `AudioSection` controls (`deviceBox`, `outputChannelBox`, `inputChannelBox`, `outputLevel`,
  `auditionButton`) are placeholders; expose them the way `OptionsSection` exposes
  `getIncludeSubfoldersToggle()`, reached via `Sidebar::getOptionsSection()`-style accessors.
- Space is not handled by the file list (`FileTreeView` rows return false), so a window-level
  key handler in `MainComponent` receives it.
- `StatusBar::setMessage(text, Tone::warning / error, detail)` for device errors and the
  permission-denied message.
- New test categories: add the `.cpp` to `ReampForgeTests` and the name to the `foreach` list in
  `CMakeLists.txt`.
- `juce_audio_devices` and `juce_audio_utils` are already linked into the app.

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

- **Mouse interactions not exercised automatically.** Click, shift/cmd-click, clicking L/R,
  the context menu, collapsing groups, drag-and-drop from Finder, scroll/pinch zoom, dragging the
  marker and the Add dialogs could not be driven: this Mac does not allow synthetic input
  (`osascript` keystrokes refused, error 1002) or screen capture. Keyboard handling is tested
  headlessly, selection rules in the model tests, rendering via snapshots. Needs a manual pass.
- Files inside a collapsed group stay selected, so L/R, Delete and Reset status apply to them
  even though they are hidden (same as Finder). Revisit if it confuses.
- At the maximum zoom (50 ms visible) the thumbnail (64 samples per point) looks stepped.
  Fine for level checks; raise the resolution if phase 5 wants to inspect sync visually.
- Settings round-trip test (PROMPT.md section 7) not written yet; planned for phase 3 when the
  device keys arrive.
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
