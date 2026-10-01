# Engine

Audio device layer and engine, namespace `rf::engine`. No GUI dependency: the UI observes engine
state only through snapshots (`DeviceStatus`, `EngineSnapshot`) and listener notifications on
the message thread.

- `AudioDeviceInterface`: the abstract device (types, devices, channel names, rates, buffer
  sizes, latencies, open/close, one duplex callback). Tests and phase 4's loopback device
  implement it without hardware.
- `JuceAudioDevice`: the real implementation over `juce::AudioDeviceManager` (app only).
- `DeviceSession`: resolves a saved/wanted configuration against the devices present, with
  fallbacks and plain-language warnings.
- `DuplexEngine`: the single real-time callback (phase 3: audition and meters).
- `SourceLoader`: decodes the played channel of a file into memory on its own thread.

Planned: `Take`, `SyncMeasurer`, `Resampler`, `FileWriter`, `LoopbackTestDevice`. See
`ARCHITECTURE.md`.
