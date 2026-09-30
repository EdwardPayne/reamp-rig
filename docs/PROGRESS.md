# Progress log

Single source of truth for where the project stands. Update at the end of every phase.
Phases are defined in `PROMPT.md` section 8; requirements are numbered per `PROMPT.md`.

## Status

| Phase | Scope | Status | Verified | Snapshot |
|---|---|---|---|---|
| 1 | Skeleton + theme | **Done** | 2026-09-30, clean Release build, launched, window captured | `docs/phase1.png` |
| 2 | Files + waveform | Not started | | |
| 3 | Audio device layer | Not started | | |
| 4 | Engine + batch | Not started | | |
| 5 | Sync | Not started | | |
| 6 | Polish | Not started | | |

Environment used so far: macOS 26.6 (Apple Silicon), CMake 3.27.8, Apple Clang 21, Xcode
Command Line Tools only, no Ninja. JUCE 9.0.3 fetched by CMake. Clean build about 1 minute.

## Next up: phase 2 — files + waveform

Spec: `PROMPT.md` sections 3.1 and 3.5, plus the Model tests in section 7.

Deliver:

- `Source/Model/`: `FileItem`, `FileTree` (grouped by folder), `FolderScanner` (recursive
  toggle, dedupe, skip unreadable files with a warning), `OutputNaming` can wait for phase 4.
- Drag-and-drop of files and folders onto the window (`juce::FileDragAndDropTarget`), plus
  "Add files…" and "Add folder…" buttons. "Include subfolders" checkbox, persisted.
- `FileTreeView`: collapsible folder groups with path and count; file rows with name, channel
  count, duration, sample rate, bit depth, L/R selector (stereo only, default L), status,
  progress bar. Multi-select (shift, cmd, cmd-A). L/R choice applies to all selected stereo
  files. Remove selected, reset status of selected. Keyboard: Delete, cmd-A, L, R.
- `WaveformPanel`: `juce::AudioThumbnail` with a cache, generated off the message thread.
  Per-channel display for stereo with the unselected channel dimmed. Time ruler, zoom with
  scroll/pinch, click to set an audition start point. "Recorded" lane stays a placeholder until
  phase 4.
- `Tests/`: FolderScanner tests (recursion on/off, dedupe, unreadable skipped, grouping), wired
  into `ctest`. Choose JUCE `UnitTest` or Catch2 and document the choice in `ARCHITECTURE.md`.
- Update this file, add `docs/phase2.png`.

Hooks left by phase 1 for phase 2:

- Design tokens: `Source/UI/Theme.h`, namespace `rf::ui::theme`.
- Fonts: `rf::ui::Fonts::mono(size, weight, tracking)` and `Fonts::sans(...)`.
- Buttons: `rf::ui::setButtonStyle(button, ButtonStyle::primary | secondary)`. Text fields:
  `styleTextEditor`.
- Sidebar sections are built with `addRow({{"Label", &component}}, height)`.
- `FileTreeView` and `WaveformPanel` only paint placeholders; replace their insides.

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
