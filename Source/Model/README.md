# Model

Non-audio data model, namespace `rf::model`. No GUI or audio-thread code here.

- `FileItem`: one source file (header info, L/R channel, status, progress).
- `FileTree`: the list, grouped by folder, deduplicated, with selection and the lead item.
- `FolderScanner`: turns dropped/added files and folders into readable audio files, on its own
  thread, reporting unreadable files.

Planned: `BatchQueue`, `OutputNaming` (phase 4). See `ARCHITECTURE.md`, "Model".
