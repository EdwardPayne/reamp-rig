# Model

Non-audio data model, namespace `rf::model`. No GUI or audio-thread code here.

- `FileItem`: one source file (header info, scan root, L/R channel, status, progress, warnings,
  output file).
- `FileTree`: the list, grouped by folder, deduplicated by file identity (device and inode
  where available), with selection, the lead item and the batch's channel lock.
- `FolderScanner`: turns dropped/added files and folders into readable audio files, on its own
  thread, reporting unreadable files.

- `BatchQueue`: order and state of one batch run (list order, pause/resume/stop, rate groups).
- `OutputNaming`: output names (own legal-name rules, length limit), destination folders
  (mirrored or not) and collision policies, plus the names one batch has already written.

See `ARCHITECTURE.md`, "Model".
