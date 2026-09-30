# Reamp Forge — guide for Claude sessions

Batch re-amping tool: plays DI guitar files out of an audio interface, through a hardware amp,
and records the result sample-aligned. C++20, JUCE 9, CMake. macOS first, Windows planned.

## Start here, every session

1. Read `docs/PROGRESS.md`. It says which phase is done, what was verified, what is next, and
   any open issues. Continue from its "Next up" section unless the user says otherwise.
2. `PROMPT.md` is the full specification. Its numbered sections are referenced everywhere
   (for example "section 3.6" = sync). Decisions marked **DECIDED** there are final.
3. `ARCHITECTURE.md` explains threads and data flow. Keep it current when the engine changes.

## Build, run, test

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
open "build/ReampForge_artefacts/Release/Reamp Forge.app"
ctest --test-dir build --output-on-failure        # once Tests/ has tests (phase 2+)
```

Snapshot the UI without screen-recording permission:

```sh
"build/ReampForge_artefacts/Release/Reamp Forge.app/Contents/MacOS/Reamp Forge" --snapshot="$PWD/docs/<name>.png"
```

Verify a phase by building **and launching**, not just compiling. Look at a snapshot.

## Way of working

- Work is delivered in the six phases listed in `PROMPT.md` section 8. One phase per task.
  After a phase: update `docs/PROGRESS.md` (status table, what was done, deviations, next up),
  add a snapshot to `docs/`, and stop for review.
- Do not add features beyond the spec without asking. Do not drop hard features silently; flag
  them in `docs/PROGRESS.md` under "Open issues".
- The audio thread never allocates, locks, logs or touches files. Treat a violation as a bug.
- The GUI never reaches into engine internals; it reads snapshots and gets change notifications
  on the message thread (`ARCHITECTURE.md`).
- Visual rules live in `PROMPT.md` section 5. All colours come from `Source/UI/Theme.h`; all
  fonts from `rf::ui::Fonts`. Zero border radius, 1 px borders, no gradients, no shadows.
  Stock JUCE styling must never show.
- Namespaces: `rf::ui`, `rf::app`, later `rf::engine`, `rf::model`.

## Branches

- `develop` is the default branch. All work lands here.
- `release` documents deployed versions only. Merge `develop` into `release` when a build is
  handed over as a version, and tag it (`v0.1.0`, ...). Never develop on `release`.
- `idea.txt` in the working directory is the owner's private note and is git-ignored. Do not
  commit it or copy its contents into tracked files.

## Commits

Commit only when asked. Message style: imperative subject, short body explaining why.
