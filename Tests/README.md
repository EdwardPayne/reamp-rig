# Tests

JUCE `UnitTest` cases, built into the `ReampRigTests` console app and run through ctest
(one ctest entry per category):

```sh
ctest --test-dir build --output-on-failure
build/ReampRigTests_artefacts/Release/ReampRigTests --category=FolderScanner
```

- `FolderScannerTests.cpp`: recursion on/off, order, dedupe, unreadable files skipped and
  reported, grouping by folder (temporary directories with generated WAV files).
- `FileTreeTests.cpp`: multi-select L/R rule, selection, status reset, removal.
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
- `OutputNamingTests.cpp`, `BatchQueueTests.cpp`: naming/destination/collisions and batch order/state.

`FakeAudioDevice.h` is a scriptable `AudioDeviceInterface` (no hardware) whose `render()`
drives the callback and captures every output channel.

Still to come (PROMPT.md section 7): SyncMeasurer and the keyed sync store in the settings
round-trip (phase 5). To add a category, add the file to `ReampRigTests` and the category
name to the `foreach` list in `CMakeLists.txt`.
