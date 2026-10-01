#include "Engine/LoopbackTestDevice.h"
#include "Engine/Resampler.h"
#include "Engine/Take.h"
#include "TestHelpers.h"

namespace rf::test
{
    using namespace rf::engine;

    namespace
    {
        std::shared_ptr<const LoadedSource> makeSource (int numSamples, float level = 0.5f, double rate = 48000.0)
        {
            auto s = std::make_shared<LoadedSource>();
            s->file = juce::File ("/di/take.wav");
            s->sampleRate = s->fileSampleRate = rate;
            s->fileLengthInSamples = numSamples;
            s->samples.setSize (1, numSamples);

            for (int i = 0; i < numSamples; ++i)
                s->samples.setSample (0, i, level * std::sin ((float) i * 0.05f));

            s->peak = level;
            return s;
        }

        /** A constant (DC) source: its peak through a unity loop is exactly `value`. */
        std::shared_ptr<const LoadedSource> makeConstantSource (int numSamples, float value)
        {
            auto s = std::make_shared<LoadedSource>();
            s->file = juce::File ("/di/dc.wav");
            s->sampleRate = s->fileSampleRate = 48000.0;
            s->fileLengthInSamples = numSamples;
            s->samples.setSize (1, numSamples);
            s->samples.clear();

            for (int i = 0; i < numSamples; ++i)
                s->samples.setSample (0, i, value);

            s->peak = value;
            return s;
        }

        DeviceConfig loopbackConfig (int bufferSize)
        {
            DeviceConfig c;
            c.typeName = "Loopback";
            c.inputDevice = c.outputDevice = "Loopback Interface";
            c.sampleRate = 48000.0;
            c.bufferSize = bufferSize;
            c.outputChannel = 0;
            c.inputChannel = 0;
            return c;
        }

        /** Runs a take to the end on a loopback device; returns its result. */
        TakeResult runTake (LoopbackTestDevice& device, DuplexEngine& engine, FileWriter& writer, TakeSpec spec)
        {
            Take take;

            if (take.start (std::move (spec), engine, writer).isNotEmpty())
                return {};

            while (take.update (engine.poll()) == Take::Phase::recording)
                device.render (2048);

            const auto deadline = juce::Time::getMillisecondCounter() + 10000;

            while (take.update (engine.poll()) != Take::Phase::done && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep (1);

            return take.getResult();
        }
    }

    /*  The take machinery around the end-to-end path: record FIFO overflow, callback gaps,
        driver xruns, the silence and clip checks, padding after lost samples, cancelling
        (no file left behind), and that audition and take share the one command slot.
    */
    class TakeTests final : public juce::UnitTest
    {
    public:
        TakeTests() : juce::UnitTest ("Take", "Take") {}

        void runTest() override
        {
            TempDirectory dir;

            beginTest ("FIFO overflow: samples that do not fit are counted, the take still completes");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);

                auto stream = std::make_shared<RecordStream> (1000);   // nobody drains it
                expect (engine.startTake (makeSource (4000), 5000, stream));
                device.render (6000);

                const auto snap = engine.poll();
                expect (snap.taking);
                expect (snap.takeFinished);
                expectEquals (snap.takePosition, (juce::int64) 5000);
                expectEquals (snap.takeDropped, (juce::int64) 4000);
                expectEquals (stream->getNumReady(), 1000);
                expectEquals (stream->getNumHandled(), (juce::int64) 5000);

                engine.stopTake();
                device.setCallback (nullptr);
            }

            beginTest ("writer pads lost samples with silence to keep the exact length");
            {
                auto stream = std::make_shared<RecordStream> (100);
                std::vector<float> ones (150, 0.5f);
                stream->write (ones.data(), 150);   // 50 dropped
                stream->markComplete();

                FileWriter writer;
                WriteJob job;
                job.stream = stream;
                job.outputLength = 150;
                job.deviceRate = job.fileRate = 48000.0;
                job.file = dir.get().getChildFile ("pad.wav");
                expect (writer.start (job).isEmpty());

                const auto deadline = juce::Time::getMillisecondCounter() + 5000;
                std::optional<WriteResult> r;

                while (! (r = writer.getResult()).has_value() && juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep (1);

                expect (r.has_value() && r->ok);
                expectEquals (r->samplesWritten, (juce::int64) 150);
                expectEquals (r->paddedSamples, (juce::int64) 50);
                expectEquals (r->peak, 0.5f);
            }

            beginTest ("callback gaps are detected during a take, not outside it");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                engine.setClock ([&device] { return device.streamTime(); });
                device.setCallback (&engine);

                device.addTimeGap (0.5);    // outside a take: ignored
                device.render (2048);

                auto stream = std::make_shared<RecordStream> (1 << 16);
                expect (engine.startTake (makeSource (20000), 20512, stream));
                device.render (4096);
                expectEquals (engine.poll().takeGaps, 0, "regular callbacks are not gaps");

                device.addTimeGap (0.003);  // 8.3 ms instead of 5.3 ms: below 1.75 buffers (9.3 ms), jitter
                device.render (256 * 4);
                expectEquals (engine.poll().takeGaps, 0);

                device.addTimeGap (0.05);   // a callback 50 ms late
                device.render (256 * 4);
                expectEquals (engine.poll().takeGaps, 1);

                device.addTimeGap (0.02);
                device.render (256 * 4);
                expectEquals (engine.poll().takeGaps, 2);

                engine.stopTake();
                device.setCallback (nullptr);
            }

            beginTest ("a take with a gap is marked as a dropout; xruns show in the device status");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (128));
                DuplexEngine engine;
                engine.setClock ([&device] { return device.streamTime(); });
                device.setCallback (&engine);
                FileWriter writer;

                const auto before = device.getStatus().xrunCount;
                device.simulateXrun();
                expectEquals (device.getStatus().xrunCount, before + 1);

                TakeSpec spec;
                spec.source = makeSource (9000);
                spec.deviceRate = 48000.0;
                spec.latencySamples = device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile ("gap.wav");

                Take take;
                expect (take.start (spec, engine, writer).isEmpty());
                device.render (1024);
                device.addTimeGap (0.1);

                while (take.update (engine.poll()) == Take::Phase::recording)
                    device.render (1024);

                while (take.update (engine.poll()) != Take::Phase::done)
                    juce::Thread::sleep (1);

                expect (take.getResult().ok);
                expectEquals (take.getResult().callbackGaps, 1);
                expect (take.getResult().hadDropout());
                device.setCallback (nullptr);
            }

            beginTest ("silence and clip checks");
            {
                FileWriter writer;

                {
                    auto options = LoopbackTestDevice::Options {};
                    options.loop = false;               // nothing comes back
                    LoopbackTestDevice device (options);
                    device.open (loopbackConfig (256));
                    DuplexEngine engine;
                    device.setCallback (&engine);

                    TakeSpec spec;
                    spec.source = makeSource (5000);
                    spec.deviceRate = 48000.0;
                    spec.outputFile = dir.get().getChildFile ("silent.wav");

                    const auto r = runTake (device, engine, writer, spec);
                    expect (r.ok);
                    expect (r.silent);
                    expect (! r.clipped);
                    device.setCallback (nullptr);
                }

                {
                    auto options = LoopbackTestDevice::Options {};
                    options.gain = 4.0f;                // 0.5 * 4 = 2.0: clips
                    LoopbackTestDevice device (options);
                    device.open (loopbackConfig (256));
                    DuplexEngine engine;
                    device.setCallback (&engine);

                    TakeSpec spec;
                    spec.source = makeSource (5000);
                    spec.deviceRate = 48000.0;
                    spec.latencySamples = device.getRoundTripSamples();
                    spec.outputFile = dir.get().getChildFile ("clipped.wav");

                    const auto r = runTake (device, engine, writer, spec);
                    expect (r.ok);
                    expect (r.clipped);
                    expect (! r.silent);
                    device.setCallback (nullptr);
                }

                {
                    LoopbackTestDevice device ({});
                    device.open (loopbackConfig (256));
                    DuplexEngine engine;
                    device.setCallback (&engine);

                    TakeSpec spec;
                    spec.source = makeSource (5000, 0.5f);
                    spec.deviceRate = 48000.0;
                    spec.latencySamples = device.getRoundTripSamples();
                    spec.outputFile = dir.get().getChildFile ("normal.wav");

                    const auto r = runTake (device, engine, writer, spec);
                    expect (r.ok && ! r.clipped && ! r.silent);
                    expectWithinAbsoluteError (r.peak, 0.5f, 0.01f);
                    device.setCallback (nullptr);
                }
            }

            // Review 2026-10-01, E5: the thresholds themselves, not only 0 and 2.0. The writer
            // measures the peak of the float samples it writes, so a DC source through a unity
            // loop has exactly the source's value as its peak.
            beginTest ("silence and clip thresholds: just below and at each threshold");
            {
                FileWriter writer;
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);

                struct Case { float level; bool silent, clipped; };
                const Case cases[] = {
                    { Take::silenceThreshold * 0.99f, true,  false },   // -60.09 dBFS: silence
                    { Take::silenceThreshold,         false, false },   // exactly -60 dBFS: not silence
                    { Take::silenceThreshold * 1.01f, false, false },
                    { 0.9998f,                        false, false },   // just below full scale
                    { DuplexEngine::clipLevel,        false, true  },   // at the clip level: clipped
                    { 1.0f,                           false, true  },
                };

                for (const auto& c : cases)
                {
                    TakeSpec spec;
                    spec.source = makeConstantSource (3000, c.level);
                    spec.deviceRate = 48000.0;
                    spec.latencySamples = device.getRoundTripSamples();
                    spec.bitsPerSample = 32;
                    spec.replaceExisting = true;
                    spec.outputFile = dir.get().getChildFile ("threshold.wav");

                    const auto r = runTake (device, engine, writer, spec);
                    const auto label = "level " + juce::String (c.level, 7);
                    expect (r.ok, label);
                    expectEquals (r.peak, c.level, label);
                    expect (r.silent == c.silent, label + (c.silent ? " is silence" : " is not silence"));
                    expect (r.clipped == c.clipped, label + (c.clipped ? " clips" : " does not clip"));
                }

                device.setCallback (nullptr);
                engine.poll();
            }

            // E5: the resampled capture margin. The amp keeps ringing after the source ends, so
            // the last output samples of a resampled take depend on input recorded after the
            // source; the capture must be long enough (half width + 2) for them to see it. The
            // writer's output must equal the offline conversion of the never-ending ring.
            beginTest ("resampled capture margin: the loop keeps ringing past the source");
            {
                for (const auto& rates : { std::pair<double, double> { 44100.0, 48000.0 }, std::pair<double, double> { 48000.0, 44100.0 },
                                           std::pair<double, double> { 44100.0, 96000.0 } })
                {
                    const auto fileRate = rates.first, deviceRate = rates.second;
                    constexpr int fileLength = 4410;
                    constexpr int latency = 123;

                    auto source = std::make_shared<LoadedSource>();
                    source->sampleRate = deviceRate;
                    source->fileSampleRate = fileRate;
                    source->fileLengthInSamples = fileLength;
                    source->resampled = true;
                    source->samples.setSize (1, (int) Resampler::getOutputLength (fileLength, fileRate, deviceRate));
                    source->samples.clear();

                    TakeSpec spec;
                    spec.source = source;
                    spec.deviceRate = deviceRate;
                    spec.latencySamples = latency;
                    spec.tailSamples = 0;

                    const auto recordLength = Take::getRecordLength (spec);
                    const auto ringLength = recordLength - latency;

                    // The ring as the input sees it after the latency, much longer than captured.
                    std::vector<float> ring ((size_t) (ringLength + 4096));

                    for (size_t i = 0; i < ring.size(); ++i)
                        ring[i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 997.0f * (float) i / (float) deviceRate);

                    auto stream = std::make_shared<RecordStream> ((int) recordLength + 16);
                    std::vector<float> zeros ((size_t) latency, 0.0f);
                    stream->write (zeros.data(), latency);
                    stream->write (ring.data(), (int) ringLength);   // exactly what the engine captures
                    stream->markComplete();

                    FileWriter writer;
                    WriteJob job;
                    job.stream = stream;
                    job.discardSamples = latency;
                    job.outputLength = Take::getOutputLength (spec);
                    job.deviceRate = deviceRate;
                    job.fileRate = fileRate;
                    job.bitsPerSample = 32;
                    job.file = dir.get().getChildFile ("margin.wav");
                    job.replaceExisting = true;

                    std::vector<float> written ((size_t) job.outputLength, 0.0f);
                    job.onWritten = [&written] (juce::int64 start, const float* data, int n)
                    {
                        std::copy (data, data + n, written.begin() + (std::ptrdiff_t) start);
                    };

                    expect (writer.start (job).isEmpty());

                    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
                    std::optional<WriteResult> r;

                    while (! (r = writer.getResult()).has_value() && juce::Time::getMillisecondCounter() < deadline)
                        juce::Thread::sleep (1);

                    expect (r.has_value() && r->ok);

                    std::vector<float> expected ((size_t) job.outputLength);
                    Resampler (deviceRate, fileRate).process (ring.data(), (juce::int64) ring.size(), expected.data(), 0, job.outputLength);

                    auto worst = 0.0f;

                    for (size_t i = 0; i < expected.size(); ++i)
                        worst = juce::jmax (worst, std::abs (written[i] - expected[i]));

                    const auto label = juce::String (fileRate / 1000.0, 1) + " kHz file via " + juce::String (deviceRate / 1000.0, 1) + " kHz";
                    logMessage ("    " + label + ": last samples vs the endless ring, max error "
                                + juce::String (juce::Decibels::gainToDecibels (worst, -400.0f), 1) + " dBFS");
                    expectEquals (worst, 0.0f, label + ": every output sample saw real input");
                }
            }

            beginTest ("cancel mid-take leaves neither the final nor the temp file");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);
                FileWriter writer;

                TakeSpec spec;
                spec.source = makeSource (48000);
                spec.deviceRate = 48000.0;
                spec.outputFile = dir.get().getChildFile ("cancelled/out.wav");

                Take take;
                expect (take.start (spec, engine, writer).isEmpty());
                device.render (8192);
                expect (take.update (engine.poll()) == Take::Phase::recording);
                take.cancel();

                expect (take.getResult().cancelled);
                expect (! engine.isTaking());
                expect (! spec.outputFile.exists());
                expect (! FileWriter::getTempFile (spec.outputFile).exists());

                const auto out = device.render (512);
                expect (out.getMagnitude (0, 0, 512) == 0.0f, "output is silent after cancelling");
                device.setCallback (nullptr);
            }

            beginTest ("an unwritable destination fails before anything plays");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);
                FileWriter writer;

                const auto blocker = dir.get().getChildFile ("not a folder");
                blocker.replaceWithText ("x");

                TakeSpec spec;
                spec.source = makeSource (1000);
                spec.deviceRate = 48000.0;
                spec.outputFile = blocker.getChildFile ("out.wav");

                Take take;
                expect (take.start (spec, engine, writer).isNotEmpty());
                expect (! engine.isTaking());
                device.setCallback (nullptr);
            }

            beginTest ("audition and take share the command slot");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);

                expect (engine.startAudition (makeSource (5000), 0));
                expect (engine.isAuditioning() && ! engine.isTaking());

                auto stream = std::make_shared<RecordStream> (1 << 14);
                expect (! engine.startTake (makeSource (5000), 4000, stream), "record length shorter than the source");
                expect (engine.startTake (makeSource (5000), 6000, stream));
                expect (engine.isTaking() && ! engine.isAuditioning());

                engine.stopAudition();   // does not stop a take
                expect (engine.isTaking());
                engine.stopTake();
                expect (! engine.isTaking());
                device.setCallback (nullptr);
            }

            //==============================================================================
            // Review 2026-10-01, E1: after the stream stopped, a command published while it was
            // stopped is new even when it happens to be allocated at the freed command's address.
            beginTest ("stop, stopTake, startTake, start: the new take begins at sample 0");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);

                auto first = std::make_shared<RecordStream> (1 << 16);
                expect (engine.startTake (makeSource (20000), 30000, first));
                device.render (256 * 10);
                expectEquals (first->getPosition(), (juce::int64) 2560);

                // Allocated before the stop, as the batch does (source loaded, stream sized).
                auto second = std::make_shared<RecordStream> (1 << 16);
                const auto secondSource = makeSource (20000, 0.25f);

                device.close();                 // the device stops (unplugged, Pause)
                engine.stopTake();              // retired and freed at once: no stream runs
                expect (engine.startTake (secondSource, 30000, second));
                device.open (loopbackConfig (256));   // reconnect, Resume

                const auto out = device.render (256);
                expectEquals (second->getPosition(), (juce::int64) 256, "the new take captured one block from 0");

                auto firstBlockMatches = true;

                for (int i = 0; i < 256; ++i)
                    firstBlockMatches = firstBlockMatches && juce::exactlyEqual (out.getSample (0, i), secondSource->samples.getSample (0, i));

                expect (firstBlockMatches, "the output plays the new source from sample 0");

                engine.stopTake();
                device.setCallback (nullptr);
                engine.poll();
            }

            // E2: a stream restart in the middle of a take is never carried through silently.
            beginTest ("a stream restart mid-take with the same configuration marks the take as a dropout");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);
                FileWriter writer;

                TakeSpec spec;
                spec.source = makeSource (24000);
                spec.deviceRate = 48000.0;
                spec.latencySamples = device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile ("restart-same.wav");

                Take take;
                expect (take.start (spec, engine, writer).isEmpty());
                device.render (8192);
                take.update (engine.poll());
                device.open (loopbackConfig (256));     // the driver restarts the stream

                while (take.update (engine.poll()) == Take::Phase::recording)
                    device.render (2048);

                const auto deadline = juce::Time::getMillisecondCounter() + 10000;

                while (take.update (engine.poll()) != Take::Phase::done && juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep (1);

                const auto& r = take.getResult();
                expect (r.ok, "same rate and buffer: the take completes");
                expect (r.hadDropout(), "but it is marked XR: the restart left a gap");
                expectEquals (r.streamRestarts, 1);
                expect (! r.deviceChanged);
                device.setCallback (nullptr);
                engine.poll();
            }

            for (const auto& variant : { std::pair<int, double> { 1024, 48000.0 }, std::pair<int, double> { 256, 44100.0 } })
            {
                const auto bufferChanged = variant.first != 256;
                beginTest (juce::String ("a stream restart mid-take with another ") + (bufferChanged ? "buffer size" : "sample rate")
                           + " aborts the take with an error");

                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);
                FileWriter writer;

                TakeSpec spec;
                spec.source = makeSource (24000);
                spec.deviceRate = 48000.0;
                spec.latencySamples = device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile (bufferChanged ? "restart-buffer.wav" : "restart-rate.wav");

                Take take;
                expect (take.start (spec, engine, writer).isEmpty());
                device.render (8192);
                take.update (engine.poll());

                auto changed = loopbackConfig (variant.first);
                changed.sampleRate = variant.second;
                device.open (changed);

                const auto deadline = juce::Time::getMillisecondCounter() + 10000;

                while (take.update (engine.poll()) != Take::Phase::done && juce::Time::getMillisecondCounter() < deadline)
                {
                    device.render (2048);
                    juce::Thread::sleep (1);
                }

                const auto& r = take.getResult();
                expect (take.getPhase() == Take::Phase::done);
                expect (! r.ok, "the take is not reported as done");
                expect (! r.cancelled);
                expect (r.error.isNotEmpty(), "with an error that says why");
                expect (r.deviceChanged);
                expect (r.error.contains ("sample rate or buffer size"));
                expect (! spec.outputFile.exists(), "no misaligned file is left");
                expect (! FileWriter::getTempFile (spec.outputFile).exists());
                device.setCallback (nullptr);
                engine.poll();
            }

            // E3: cancel() after the writer has already renamed the file must not leave it.
            beginTest ("cancel after the writer has finished leaves no final file");
            {
                LoopbackTestDevice device ({});
                device.open (loopbackConfig (256));
                DuplexEngine engine;
                device.setCallback (&engine);
                FileWriter writer;

                TakeSpec spec;
                spec.source = makeSource (5000);
                spec.deviceRate = 48000.0;
                spec.latencySamples = device.getRoundTripSamples();
                spec.outputFile = dir.get().getChildFile ("cancel-after-finish.wav");

                Take take;
                expect (take.start (spec, engine, writer).isEmpty());

                while (take.update (engine.poll()) == Take::Phase::recording)
                    device.render (2048);

                const auto deadline = juce::Time::getMillisecondCounter() + 10000;

                while (! writer.getResult().has_value() && juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep (1);

                expect (writer.getResult().has_value() && writer.getResult()->ok, "the writer finished (renamed the file)");
                take.cancel();

                expect (take.getResult().cancelled);
                expect (! take.getResult().ok);
                expect (! spec.outputFile.exists(), "a cancelled take never leaves its final file");
                expect (! FileWriter::getTempFile (spec.outputFile).exists());
                device.setCallback (nullptr);
                engine.poll();
            }

            beginTest ("record lengths");
            {
                TakeSpec spec;
                spec.source = makeSource (1000);
                spec.deviceRate = 48000.0;
                spec.latencySamples = 300;
                spec.tailSamples = 50;
                expectEquals (Take::getRecordLength (spec), (juce::int64) 1350);
                expectEquals (Take::getOutputLength (spec), (juce::int64) 1050);
            }
        }
    };

    static TakeTests takeTests;
}
