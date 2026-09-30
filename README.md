# Reamp Forge

Batch re-amping of guitar DI tracks through a hardware amp via an audio interface.
Desktop app, C++20 + JUCE 9.0.3 (fetched automatically), CMake.

Status: **phase 1 (skeleton + theme)**. The window, layout and look-and-feel are in place with
placeholder content; nothing is functional yet.

## Requirements

- macOS 11+ on Apple Silicon or Intel (Windows is planned, untested)
- CMake 3.22 or newer
- Xcode Command Line Tools (Apple Clang). Full Xcode is not needed.
- Network access on the first configure (CMake downloads JUCE 9.0.3, about 25 MB)

## Build

From the repository root:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
```

The app bundle is written to:

```
build/ReampForge_artefacts/Release/Reamp Forge.app
```

It is ad-hoc code signed as a post-build step, which is enough for local runs.

A clean Release build takes about 1 minute on an Apple Silicon Mac (configure about 20 s,
most of which is downloading JUCE and building its `juceaide` helper).

### Note on broken Command Line Tools installs

If `/Library/Developer/CommandLineTools/usr/include/c++/v1` exists but contains only a few
leftover folders (no `algorithm` header), Apple Clang picks it up instead of the SDK's libc++
headers and even `#include <algorithm>` fails. `CMakeLists.txt` detects this and adds
`-nostdinc++ -isystem <SDK>/usr/include/c++/v1` automatically, printing a CMake warning.
The permanent fix is to remove that stale directory or reinstall the Command Line Tools:

```sh
sudo rm -rf /Library/Developer/CommandLineTools/usr/include/c++
```

## Run

```sh
open "build/ReampForge_artefacts/Release/Reamp Forge.app"
```

or, to see stdout/stderr in the terminal:

```sh
"build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge"
```

### UI snapshot (development aid)

The app can render its own window (including open popup menus and tooltips) to a PNG and quit.
This works without granting the terminal screen-recording permission:

```sh
"build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge" --snapshot="$PWD/docs/phase1.png"
```

The native macOS title bar is not part of the snapshot.

## Testing with the built-in mic and speakers

Planned for phase 3 (audio device layer). On macOS the built-in microphone and speakers are
separate CoreAudio devices; the app will allow a separate input/output device pair for testing
and label it "not sample-synchronized, for testing only". The Info.plist already contains
`NSMicrophoneUsageDescription`, so macOS will ask for microphone access on first use.

## Layout

```
CMakeLists.txt
Assets/Fonts/     JetBrains Mono + Inter (Regular/Medium/Bold) and their OFL licences
Source/Main.cpp   application entry point
Source/App/       main window, root component, macOS appearance, snapshot aid
Source/UI/        Theme (design tokens), Fonts, LookAndFeel, TopBar, FileTreeView,
                  Sidebar + sections, WaveformPanel, StatusBar
Source/Engine/    (empty, phases 3-5)
Source/Model/     (empty, phase 2+)
Tests/            (empty, phase 2+)
```

See `ARCHITECTURE.md` for the planned thread and data-flow design, `PROMPT.md` for the full
specification, and `docs/PROGRESS.md` for the current status and what comes next.

## Branches

- `develop`: default branch, all development happens here.
- `release`: deployed versions only. `develop` is merged into it and tagged when a version is
  handed over.

## Licences

- JUCE 9: AGPLv3 / commercial dual licence (used here under AGPLv3 for a personal tool).
- JetBrains Mono and Inter: SIL Open Font License 1.1 (see `Assets/Fonts/*-OFL.txt`).
