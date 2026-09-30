# Tests

JUCE `UnitTest` cases, built into the `ReampForgeTests` console app and run through ctest
(one ctest entry per category):

```sh
ctest --test-dir build --output-on-failure
build/ReampForgeTests_artefacts/Release/ReampForgeTests --category=FolderScanner
```

- `FolderScannerTests.cpp`: recursion on/off, order, dedupe, unreadable files skipped and
  reported, grouping by folder (temporary directories with generated WAV files).
- `FileTreeTests.cpp`: multi-select L/R rule, selection, status reset, removal.
- `FileTreeViewTests.cpp`: the file list's keyboard handling, headless.

Still to come (PROMPT.md section 7): OutputNaming, SyncMeasurer, the loopback engine test and
the settings round-trip. To add a category, add the file to `ReampForgeTests` and the category
name to the `foreach` list in `CMakeLists.txt`.
