# Engine

Audio device layer and engine, namespace `rf::engine`. No GUI dependency: the UI observes engine
state only through snapshots (`DeviceStatus`, `EngineSnapshot`) and listener notifications on
the message thread.

- `AudioDeviceInterface`: the abstract device (types, devices, channel names, rates, buffer
  sizes, latencies, open/close, one duplex callback). Tests and phase 4's loopback device
  implement it without hardware (`LoopbackTestDevice`, `Tests/FakeAudioDevice.h`).
- `JuceAudioDevice`: the real implementation over `juce::AudioDeviceManager` (app only).
- `DeviceSession`: resolves a saved/wanted configuration against the devices present, with
  fallbacks and plain-language warnings.
- `DuplexEngine`: the single real-time callback (audition, the take, meters, gap detection).
- `RecordStream`: the lock-free record FIFO of one take (plus its counters).
- `Take`: one take's lengths, FIFO, writer job and engine command; silence/clip checks.
- `FileWriter`: the writer thread (latency discard, resampling back, exact PCM, temp + rename).
- `Resampler` / `ResamplerStream`: high-quality, sample-0-aligned sample-rate conversion.
- `SourceLoader`: decodes the played channel of a file into memory on its own thread.
- `LoopbackTestDevice`: simulated interface with output looped back to input (tests and
  `--virtual-device`).

Planned: `SyncMeasurer` (phase 5). See `ARCHITECTURE.md`.
