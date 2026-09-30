# Engine

Audio device layer and engine (phases 3 and 4). No GUI dependency: the UI observes engine state only through a
thread-safe snapshot/listener interface.

Planned: `AudioDeviceInterface`, `DuplexEngine`, `Take`, `SyncMeasurer`, `Resampler`,
`FileWriter`, `LoopbackTestDevice`. See `ARCHITECTURE.md`.
