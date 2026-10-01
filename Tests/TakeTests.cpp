#include "Engine/LoopbackTestDevice.h"
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
