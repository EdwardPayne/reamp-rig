# Tests

JUCE `UnitTest` cases, built into the `ReampRigTests` console app and run through ctest
(one ctest entry per category):

```sh
ctest --test-dir build --output-on-failure
build/ReampRigTests_artefacts/Release/ReampRigTests --category=FolderScanner
```

- `FolderScannerTests.cpp`: recursion on/off, order, dedupe, unreadable files skipped and
  reported, grouping by folder (temporary directories with generated WAV files).
- `FileTreeTests.cpp`: multi-select L/R rule, selection (and deselecting a collapsed group),
  status reset, removal.
- `FileTreeViewTests.cpp`: the file list's keyboard handling, headless.
- `SettingsTests.cpp`: round-trip of every persisted key (temporary settings file).
- `DeviceSessionTests.cpp`: device/channel resolution and fallbacks, with `FakeAudioDevice.h`.
- `DuplexEngineTests.cpp`: audition rendering (channel, gain, start, silence elsewhere) and
  meters, driven through `FakeAudioDevice.h`.
- `SourceLoaderTests.cpp`: channel decode, peak, resampling alignment, failures.
- `ResamplerTests.cpp`: lengths, exact positions, error on a test sine (documented in
  `ARCHITECTURE.md`), round trip, streaming identical to offline.
- `LoopbackTests.cpp` (category `Loopback`): the end-to-end engine test with
  `engine::LoopbackTestDevice`: files on disk -> engine -> loop -> writer -> files, exact length and
  bit-exact for mono/stereo L/R, buffers 64/256/480/1024, delays 0/1/37/256/1000, plus gain, tail,
  a long delay, 16/32f formats and a resampled take.
- `TakeTests.cpp`: FIFO overflow, padding, callback gaps, xruns, silence/clip, cancel, failures.
- `OutputNamingTests.cpp`, `BatchQueueTests.cpp`: naming/destination/collisions and batch order/state
  (including the pauses between files the ETA still has to count).
- `SyncTests.cpp` (category `Sync`): the sync test signal, the correlation analysis, the combination
  of repeats, measurements on the loopback device and the per-rate lookup (`SyncPlan`).
- `KeyboardTests.cpp` (category `Keyboard`): shortcuts in every state: the confirmation dialog
  (Return / Escape, everything else swallowed, command shortcuts passed on), the list during a
  batch, and collapsed groups (deselected, never part of cmd-A or L / R / Delete).

`FakeAudioDevice.h` is a scriptable `AudioDeviceInterface` (no hardware) whose `render()`
drives the callback and captures every output channel.

`SettingsTests.cpp` also covers the keyed sync store (including Forget), the pause between
files, the window bounds and their clamping to the displays; `DeviceSessionTests.cpp` covers
when a device that came back is reopened (`ReconnectWatch`); `FolderScannerTests.cpp` covers
skipping the output subfolders.

To add a category, add the file to `ReampRigTests` and the category name to the `foreach` list
in `CMakeLists.txt`.
