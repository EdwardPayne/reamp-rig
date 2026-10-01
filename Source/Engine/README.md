# Engine

Audio device layer and engine, namespace `rf::engine`. No GUI dependency: the UI observes engine
state only through snapshots (`DeviceStatus`, `EngineSnapshot`) and listener notifications on
the message thread.

- `AudioDeviceInterface`: the abstract device (types, devices, channel names, rates, buffer
  sizes, latencies, open/close, one duplex callback). Tests and phase 4's loopback device
  implement it without hardware (`LoopbackTestDevice`, `Tests/FakeAudioDevice.h`).
- `JuceAudioDevice`: the real implementation over `juce::AudioDeviceManager` (app only).
- `DeviceSession`: resolves a saved/wanted configuration against the devices present, with
  fallbacks and plain-language warnings; `Mode::exact` (no fallback at all) for the sample-rate
  switches of a batch or a sync run. `ReconnectWatch` (same header): decides when the saved
  device, lost or missing at launch, is reopened by itself.
- `DuplexEngine`: the single real-time callback (audition, the take, meters, gap detection,
  stream restarts under a take). New commands are recognised by generation, not address.
- `RecordStream`: the lock-free record FIFO of one take (plus its counters: dropped samples,
  callback gaps, stream restarts).
- `Take`: one take's lengths, FIFO, writer job and engine command; silence/clip checks; a
  restart at another rate or buffer size discards the take; cancel never leaves a file.
- `FileWriter`: the writer thread (latency discard, resampling back, exact PCM, temp + rename).
- `Resampler` / `ResamplerStream`: high-quality, sample-0-aligned sample-rate conversion.
- `SourceLoader`: decodes the played channel of a file into memory on its own thread.
- `LoopbackTestDevice`: simulated interface with output looped back to input (tests and
  `--virtual-device`); can be unplugged and can refuse sample rates (dev flags).
- `SyncMeasurer` / `SyncMeasurement`: the Sync run (test signal, five repeats, FFT
  correlation, combination) and the key and value of a stored measurement.

See `ARCHITECTURE.md`.
