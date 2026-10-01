#include "App/Settings.h"
#include "App/SyncPlan.h"
#include "Engine/SyncMeasurer.h"
#include "LoopbackRig.h"

namespace rf::test
{
    using namespace rf::engine;

    namespace
    {
        /** A DuplexEngine on the LoopbackTestDevice, driven block by block, running a whole
            sync measurement (5 repeats) to its result. */
        struct SyncRig
        {
            SyncRig (LoopbackTestDevice::Options o, double rate, int bufferSize)
                : device (std::move (o))
            {
                device.open (deviceConfig (rate, bufferSize));
                engine.setClock ([this] { return device.streamTime(); });   // gaps only when a test adds one
                device.setCallback (&engine);
            }

            ~SyncRig()
            {
                device.setCallback (nullptr);
                engine.poll();
            }

            SyncResult measure (SyncOptions options = {}, const std::function<void (SyncMeasurer&)>& beforeBlock = {})
            {
                SyncMeasurer m;
                lastStartError = m.start (options, device.getStatus().config.sampleRate, engine);

                if (lastStartError.isNotEmpty())
                    return {};

                const auto block = device.getStatus().config.bufferSize * 8;
                const auto deadline = juce::Time::getMillisecondCounter() + 60000;

                while (m.update (engine.poll()) != SyncMeasurer::Phase::done && juce::Time::getMillisecondCounter() < deadline)
                {
                    if (engine.isTaking())
                    {
                        if (beforeBlock != nullptr)
                            beforeBlock (m);

                        device.render (block);
                    }
                    else
                    {
                        juce::Thread::sleep (1);   // the worker is analysing
                    }
                }

                repeatsRun = (int) m.getResult().repeats.size();
                return m.getResult();
            }

            LoopbackTestDevice device;
            DuplexEngine engine;
            juce::String lastStartError;
            int repeatsRun = 0;
        };

        juce::String describe (const SyncResult& r)
        {
            juce::StringArray delays;

            for (const auto& rep : r.repeats)
                delays.add (juce::String (rep.delay) + " (" + SyncMeasurer::describe (rep.outcome) + ", "
                            + juce::String (rep.peakToSidelobeDb, 1) + " dB)");

            return (r.ok ? "ok " + juce::String (r.measurement.samples) + " smp, " + toString (r.measurement.confidence)
                         : "failed: " + r.summary)
                 + " | repeats: " + delays.joinIntoString (", ");
        }

        SyncRepeat okRepeat (int delay, double psr = 30.0)
        {
            SyncRepeat r;
            r.outcome = SyncRepeat::Outcome::ok;
            r.delay = delay;
            r.peakToSidelobeDb = psr;
            r.returnedPeak = 0.25f;
            r.recordingPeak = 0.25f;
            return r;
        }

        SyncRepeat failedRepeat (SyncRepeat::Outcome o)
        {
            SyncRepeat r;
            r.outcome = o;
            return r;
        }
    }

    /*  SyncMeasurer (PROMPT.md 3.6 and section 7): the test signal, the correlation, the
        combination of repeats, and whole measurements against the LoopbackTestDevice (round
        trip = one buffer + delay): known delays recovered exactly, with noise, with a gain
        change, a clipped return and a silent return rejected; a measured latency used instead
        of a deliberately wrong driver estimate makes a take bit-exact; the batch's per-rate
        lookup (SyncPlan).
    */
    class SyncTests final : public juce::UnitTest
    {
    public:
        SyncTests() : juce::UnitTest ("Sync", "Sync") {}

        void runTest() override
        {
            testSignal();
            testAnalysis();
            testCombination();
            testLoopback();
            testFailures();
            testMeasuredLatencyInATake();
            testPlan();
        }

        //==============================================================================
        void testSignal()
        {
            beginTest ("test signal: click, then a 50 ms exponential sweep, peak at the level");

            for (const auto rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto s = SyncMeasurer::makeTestSignal (rate, -12.0f);
                const auto expectedLength = 1 + (int) std::lround (0.005 * rate) + (int) std::lround (0.05 * rate);
                expectEquals ((int) s.size(), expectedLength);

                const auto level = juce::Decibels::decibelsToGain (-12.0f);
                expectEquals (s[0], level);                         // the click
                expectEquals (s[1], 0.0f);                          // then silence

                auto peak = 0.0f;

                for (auto v : s)
                    peak = juce::jmax (peak, std::abs (v));

                expectWithinAbsoluteError (peak, level, 1.0e-6f);
                expectEquals (s.back(), 0.0f);                     // faded out
            }

            const auto quiet = SyncMeasurer::makeTestSignal (48000.0, -30.0f);
            expectWithinAbsoluteError (quiet[0], juce::Decibels::decibelsToGain (-30.0f), 1.0e-7f);
        }

        void testAnalysis()
        {
            beginTest ("analysis: the correlation peak is at the exact delay, also inverted");

            const auto rate = 48000.0;
            const auto signal = SyncMeasurer::makeTestSignal (rate, -12.0f);
            const auto n = 48000;

            for (const auto delay : { 1, 64, 293, 1029, 3064, 40000 })
            {
                for (const auto sign : { 1.0f, -1.0f })
                {
                    std::vector<float> rec ((size_t) n, 0.0f);

                    for (size_t i = 0; i < signal.size() && delay + (int) i < n; ++i)
                        rec[(size_t) delay + i] = sign * 0.5f * signal[i];

                    const auto r = SyncMeasurer::analyse (rec.data(), n, signal, rate);
                    expect (r.outcome == SyncRepeat::Outcome::ok, SyncMeasurer::describe (r.outcome));
                    expectEquals (r.delay, delay);
                    expect (r.inverted == (sign < 0.0f));
                    expectWithinAbsoluteError (juce::Decibels::gainToDecibels (r.returnedPeak), -18.0f, 0.05f);

                    if (delay == 293 && sign > 0.0f)
                        logMessage ("    peak-to-sidelobe ratio of a clean loop: " + juce::String (r.peakToSidelobeDb, 1) + " dB"
                                    + " (exclusion +-" + juce::String (SyncMeasurer::getExclusionSamples (rate)) + " samples)");

                    expectGreaterOrEqual (r.peakToSidelobeDb, SyncMeasurer::highPeakToSidelobeDb);
                }
            }

            beginTest ("analysis: silence, pure noise, a constant and a clipped recording");
            {
                std::vector<float> rec ((size_t) n, 0.0f);
                expect (SyncMeasurer::analyse (rec.data(), n, signal, rate).outcome == SyncRepeat::Outcome::silent);

                juce::Random random (3);

                for (auto& v : rec)
                    v = (random.nextFloat() * 2.0f - 1.0f) * 0.1f;

                const auto noise = SyncMeasurer::analyse (rec.data(), n, signal, rate);
                logMessage ("    pure noise: peak-to-sidelobe " + juce::String (noise.peakToSidelobeDb, 1) + " dB");
                expect (noise.outcome == SyncRepeat::Outcome::noPeak);

                std::fill (rec.begin(), rec.end(), 0.25f);
                expect (SyncMeasurer::analyse (rec.data(), n, signal, rate).outcome == SyncRepeat::Outcome::noPeak);

                std::fill (rec.begin(), rec.end(), 0.0f);

                for (size_t i = 0; i < signal.size(); ++i)
                    rec[500 + i] = signal[i] * 8.0f;

                expect (SyncMeasurer::analyse (rec.data(), n, signal, rate).outcome == SyncRepeat::Outcome::clipped);
            }
        }

        void testCombination()
        {
            using O = SyncRepeat::Outcome;
            const auto rate = 48000.0;

            beginTest ("combination: five agreeing repeats give the median and high confidence");
            {
                const auto r = SyncMeasurer::combine ({ okRepeat (1000), okRepeat (1000), okRepeat (1001), okRepeat (1000), okRepeat (999) },
                                                      5, rate);
                expect (r.ok);
                expectEquals (r.measurement.samples, 1000);
                expectWithinAbsoluteError (r.measurement.ms, 1000.0 / 48.0, 1.0e-9);
                expectEquals (r.measurement.repeatsUsed, 5);
                expect (r.measurement.confidence == SyncConfidence::high);
            }

            beginTest ("combination: an outlier is discarded (medium confidence)");
            {
                const auto r = SyncMeasurer::combine ({ okRepeat (100), okRepeat (100), okRepeat (101), okRepeat (100), okRepeat (180) },
                                                      5, rate);
                expect (r.ok);
                expectEquals (r.measurement.samples, 100);
                expectEquals (r.measurement.repeatsUsed, 4);
                expect (r.measurement.confidence == SyncConfidence::medium);
            }

            beginTest ("combination: three of five, or a weak peak, give low confidence");
            {
                auto r = SyncMeasurer::combine ({ okRepeat (77), okRepeat (77), failedRepeat (O::noPeak), okRepeat (77),
                                                  failedRepeat (O::noPeak) }, 5, rate);
                expect (r.ok);
                expectEquals (r.measurement.samples, 77);
                expect (r.measurement.confidence == SyncConfidence::low);

                r = SyncMeasurer::combine ({ okRepeat (77, 9.0), okRepeat (77, 9.0), okRepeat (77, 9.0), okRepeat (77, 9.0),
                                             okRepeat (77, 9.0) }, 5, rate);
                expect (r.ok && r.measurement.confidence == SyncConfidence::low);
            }

            beginTest ("combination: failures are reported, never measured");
            {
                auto r = SyncMeasurer::combine ({ okRepeat (100), okRepeat (150), okRepeat (200), okRepeat (250), okRepeat (300) }, 5, rate);
                expect (! r.ok && r.failure == SyncFailure::unstable, r.message);
                expect (r.message.contains ("100, 150, 200, 250, 300"));

                r = SyncMeasurer::combine ({ okRepeat (100), failedRepeat (O::clipped) }, 5, rate);
                expect (! r.ok && r.failure == SyncFailure::clipped);

                r = SyncMeasurer::combine ({ failedRepeat (O::silent), failedRepeat (O::silent), failedRepeat (O::silent) }, 5, rate);
                expect (! r.ok && r.failure == SyncFailure::silent);

                r = SyncMeasurer::combine ({ okRepeat (5), failedRepeat (O::tooLow), failedRepeat (O::tooLow), failedRepeat (O::tooLow) }, 5, rate);
                expect (! r.ok && r.failure == SyncFailure::tooLow);

                r = SyncMeasurer::combine ({ failedRepeat (O::dropout), failedRepeat (O::dropout), okRepeat (5), failedRepeat (O::dropout) }, 5, rate);
                expect (! r.ok && r.failure == SyncFailure::dropout);
                expectEquals (r.measurement.samples, 0);
            }
        }

        //==============================================================================
        void testLoopback()
        {
            struct Case { double rate; int buffer; int delay; };

            // Round trips 256, 101, 1480 (> buffer, > 1000), 1029, 3128, 812 and 2048 + 517.
            const Case cases[] = { { 48000.0, 256, 0 }, { 48000.0, 64, 37 }, { 48000.0, 480, 1000 }, { 44100.0, 1024, 5 },
                                   { 96000.0, 128, 3000 }, { 48000.0, 512, 300 }, { 96000.0, 1024, 1541 } };

            for (const auto& c : cases)
            {
                beginTest ("loopback " + juce::String (c.rate / 1000.0) + " kHz, buffer " + juce::String (c.buffer) + ", delay "
                           + juce::String (c.delay) + ": round trip recovered exactly");

                auto options = deviceOptions (c.delay);
                options.bufferSizes = { 64, 128, 256, 480, 512, 1024 };
                SyncRig rig (options, c.rate, c.buffer);
                const auto expected = rig.device.getRoundTripSamples();
                expectEquals (expected, c.buffer + c.delay);

                const auto r = rig.measure();
                expect (rig.lastStartError.isEmpty(), rig.lastStartError);
                expect (r.ok, describe (r) + " " + r.message);
                expectEquals (r.measurement.samples, expected);
                expectWithinAbsoluteError (r.measurement.ms, expected * 1000.0 / c.rate, 1.0e-9);
                expectEquals (r.measurement.repeatsUsed, 5);
                expectEquals ((int) r.repeats.size(), 5);
                expect (r.measurement.confidence == SyncConfidence::high, describe (r));
                expectWithinAbsoluteError (r.measurement.returnedPeakDb, -12.0f, 0.05f);
                expect (! r.inverted);
                expect (! rig.engine.isTaking());
            }

            beginTest ("loopback with noise (+-0.02, about -34 dBFS) recovers the exact delay");
            {
                SyncRig rig (deviceOptions (293, 1.0f, 0.02f), 48000.0, 256);
                const auto r = rig.measure();
                logMessage ("    " + describe (r));
                expect (r.ok, r.message);
                expectEquals (r.measurement.samples, 256 + 293);
                expect (r.measurement.confidence != SyncConfidence::low);
            }

            beginTest ("loopback with a gain change (-12 dB in the loop, sync level -6 dBFS)");
            {
                SyncRig rig (deviceOptions (1000, 0.25f), 48000.0, 256);
                SyncOptions options;
                options.levelDb = -6.0f;
                const auto r = rig.measure (options);
                expect (r.ok, r.message);
                expectEquals (r.measurement.samples, 1256);
                expectWithinAbsoluteError (r.measurement.returnedPeakDb, -6.0f - 12.04f, 0.1f);
            }

            beginTest ("the output level control does not change the test signal, and is restored");
            {
                SyncRig rig (deviceOptions (37), 48000.0, 256);
                rig.engine.setGainDb (-20.0f);
                const auto r = rig.measure();
                expect (r.ok, r.message);
                expectWithinAbsoluteError (r.measurement.returnedPeakDb, -12.0f, 0.05f);
                expectEquals (rig.engine.getGainDb(), -20.0f);
            }

            beginTest ("a long driver estimate lengthens the recording so a long round trip still fits");
            {
                auto options = deviceOptions (60000);       // 1.25 s at 48 kHz: longer than the default 1 s
                SyncRig rig (options, 48000.0, 256);
                SyncOptions sync;
                sync.latencyHint = 60256;
                const auto r = rig.measure (sync);
                expect (r.ok, r.message);
                expectEquals (r.measurement.samples, 60256);
            }

            beginTest ("one dropout is discarded with its repeat (four repeats agree: medium)");
            {
                SyncRig rig (deviceOptions (37), 48000.0, 256);
                auto gapAdded = false;
                const auto r = rig.measure ({}, [&] (SyncMeasurer& m)
                {
                    if (! gapAdded && m.getRepeatsDone() == 2 && m.getProgress() > 0.5)
                    {
                        rig.device.addTimeGap (0.1);
                        gapAdded = true;
                    }
                });

                expect (gapAdded);
                expect (r.ok, r.message);
                expectEquals (r.measurement.samples, 293);
                expectEquals (r.measurement.repeatsUsed, 4);
                expect (r.repeats[2].outcome == SyncRepeat::Outcome::dropout);
                expect (r.measurement.confidence == SyncConfidence::medium);
            }

            beginTest ("cancel stops the take, restores the gain and stores nothing");
            {
                SyncRig rig (deviceOptions (37), 48000.0, 256);
                rig.engine.setGainDb (-3.0f);
                SyncMeasurer m;
                expect (m.start ({}, 48000.0, rig.engine).isEmpty());
                rig.device.render (2048);
                m.update (rig.engine.poll());
                expect (m.isRunning());
                m.cancel();
                expect (m.getPhase() == SyncMeasurer::Phase::done);
                expect (! m.getResult().ok && m.getResult().failure == SyncFailure::cancelled);
                expect (! rig.engine.isTaking());
                expectEquals (rig.engine.getGainDb(), -3.0f);
            }
        }

        void testFailures()
        {
            beginTest ("a clipped return is rejected after the first repeat");
            {
                SyncRig rig (deviceOptions (100, 8.0f), 48000.0, 256);    // -12 dBFS * 8 = +6 dBFS
                const auto r = rig.measure();
                logMessage ("    " + r.message);
                expect (! r.ok);
                expect (r.failure == SyncFailure::clipped);
                expectEquals (rig.repeatsRun, 1);
                expectEquals (r.measurement.samples, 0);
            }

            beginTest ("a silent return (nothing connected) is rejected");
            {
                auto options = deviceOptions (100);
                options.loop = false;
                SyncRig rig (options, 48000.0, 256);
                const auto r = rig.measure();
                logMessage ("    " + r.message);
                expect (! r.ok);
                expect (r.failure == SyncFailure::silent);
                expectEquals (rig.repeatsRun, 3);       // three failures: five repeats can no longer succeed
            }

            beginTest ("a return far too quiet (-78 dBFS) is rejected as too low");
            {
                SyncRig rig (deviceOptions (100, juce::Decibels::decibelsToGain (-66.0f)), 48000.0, 256);
                const auto r = rig.measure();
                logMessage ("    " + r.message);
                expect (! r.ok);
                expect (r.failure == SyncFailure::tooLow);
            }

            beginTest ("only noise comes back (nothing connected, a noisy input): no clear peak");
            {
                SyncRig rig (deviceOptions (100, 0.0f, 0.05f), 48000.0, 256);     // loop gain 0, noise +-0.05
                const auto r = rig.measure();
                logMessage ("    " + r.message);
                expect (! r.ok);
                expect (r.failure == SyncFailure::noPeak);
            }

            beginTest ("no device rate: refused");
            {
                DuplexEngine engine;
                SyncMeasurer m;
                expect (m.start ({}, 0.0, engine).isNotEmpty());
                expect (! m.isRunning());
            }
        }

        //==============================================================================
        void testMeasuredLatencyInATake()
        {
            beginTest ("a wrong driver estimate: the take uses the measurement and comes back bit-exact");

            TempDirectory dir;
            const auto src = dir.get().getChildFile ("src/riff.wav");
            constexpr int length = 9000;
            expect (writeTone (src, 2, 48000.0, length));
            const auto ref = readFile (src);

            auto options = deviceOptions (37);
            options.reportedInputLatency = 10;      // the driver claims 30 samples; the loop is 293
            options.reportedOutputLatency = 20;

            Rig rig (options, 48000.0, 256);
            const auto status = rig.device.getStatus();
            expectEquals (status.inputLatencySamples + status.outputLatencySamples, 30);

            // Measure on the same engine and device (the take rig).
            SyncMeasurer m;
            expect (m.start ({}, 48000.0, rig.engine).isEmpty());

            for (int guard = 0; m.update (rig.engine.poll()) != SyncMeasurer::Phase::done && guard < 200000; ++guard)
            {
                if (rig.engine.isTaking())
                    rig.device.render (2048);
                else
                    juce::Thread::sleep (1);
            }

            const auto measured = m.getResult();
            expect (measured.ok, measured.message);
            expectEquals (measured.measurement.samples, 293);

            // What the batch does per take: the stored measurement for this configuration wins.
            TempDirectory settingsDir;
            app::Settings settings (settingsDir.get().getChildFile ("s.settings"));
            settings.setSyncMeasurement (SyncKey::from (status), measured.measurement);

            const auto choice = app::syncplan::chooseLatency (status, settings.getSyncMeasurement (SyncKey::from (status)));
            expect (choice.measured);
            expectEquals (choice.samples, 293);

            const auto estimate = app::syncplan::chooseLatency (status, std::nullopt);
            expect (! estimate.measured);
            expectEquals (estimate.samples, 30);

            auto takeWith = [&] (app::syncplan::LatencyChoice c, const juce::String& name)
            {
                TakeSpec spec;
                spec.source = load (src, 1, 48000.0);
                spec.deviceRate = 48000.0;
                spec.latencySamples = c.samples;
                spec.latencyMeasured = c.measured;
                spec.outputFile = dir.get().getChildFile ("out/" + name + ".wav");

                juce::String error;
                const auto result = rig.run (spec, error);
                expect (result.ok, error + result.error);
                expect (result.notCalibrated == ! c.measured);
                return readFile (spec.outputFile);
            };

            const auto good = takeWith (choice, "measured");
            expectEquals (good.getNumSamples(), length);
            auto exact = good.getNumSamples() == length;

            for (int i = 0; exact && i < length; ++i)
                exact = juce::exactlyEqual (good.getSample (0, i), ref.getSample (1, i));

            expect (exact, "with the measured latency the take must be bit-exact");

            // With the estimate the take is the source late by the estimate's error (263 samples).
            const auto bad = takeWith (estimate, "estimated");
            expectEquals (bad.getNumSamples(), length);
            const auto offset = 293 - 30;
            auto differs = false, shifted = bad.getNumSamples() == length;

            for (int i = 0; i < juce::jmin (length, bad.getNumSamples()); ++i)
            {
                differs = differs || ! juce::exactlyEqual (bad.getSample (0, i), ref.getSample (1, i));
                const auto expected = i >= offset ? ref.getSample (1, i - offset) : 0.0f;
                shifted = shifted && juce::exactlyEqual (bad.getSample (0, i), expected);
            }

            expect (differs, "the estimate must misalign the take");
            expect (shifted, "the misalignment is exactly the estimate's error");
        }

        void testPlan()
        {
            beginTest ("batch plan: each rate the batch switches to is its own key; lookups per rate");

            DeviceStatus status;
            status.isOpen = true;
            status.config.typeName = "CoreAudio";
            status.config.inputDevice = status.config.outputDevice = "Apollo Twin";
            status.config.sampleRate = 48000.0;
            status.config.bufferSize = 256;
            status.sampleRates = { 44100.0, 48000.0, 96000.0 };
            status.inputLatencySamples = 100;
            status.outputLatencySamples = 50;

            expectEquals (app::syncplan::deviceRateFor (44100.0, status), 44100.0);
            expectEquals (app::syncplan::deviceRateFor (22050.0, status), 48000.0);   // resampled at the current rate

            const auto keys = app::syncplan::keysForBatch (status, { 96000.0, 96000.0, 44100.0, 22050.0, 48000.0 });
            expectEquals ((int) keys.size(), 3);
            expectEquals (keys[0].sampleRate, 48000.0);    // the current configuration first
            expectEquals (keys[1].sampleRate, 96000.0);
            expectEquals (keys[2].sampleRate, 44100.0);
            expect (keys[1].bufferSize == 256 && keys[1].outputDevice == "Apollo Twin");

            TempDirectory dir;
            app::Settings settings (dir.get().getChildFile ("plan.settings"));
            auto lookup = [&] (const SyncKey& k) { return settings.getSyncMeasurement (k); };

            expectEquals ((int) app::syncplan::missing (keys, lookup).size(), 3);

            SyncMeasurement at48;
            at48.samples = 312;
            at48.confidence = SyncConfidence::high;
            settings.setSyncMeasurement (keys[0], at48);

            SyncMeasurement at96;
            at96.samples = 598;
            at96.confidence = SyncConfidence::medium;
            settings.setSyncMeasurement (keys[1], at96);

            const auto missing = app::syncplan::missing (keys, lookup);
            expectEquals ((int) missing.size(), 1);
            expectEquals (missing[0].sampleRate, 44100.0);

            // Per take: the batch looks up the configuration that take runs in.
            auto at = [&] (double rate)
            {
                auto s = status;
                s.config.sampleRate = rate;
                return app::syncplan::chooseLatency (s, settings.getSyncMeasurement (SyncKey::from (s)));
            };

            expect (at (48000.0).measured && at (48000.0).samples == 312);
            expect (at (96000.0).measured && at (96000.0).samples == 598);
            expect (! at (44100.0).measured && at (44100.0).samples == 150);

            // Another buffer size is another configuration.
            auto other = status;
            other.config.bufferSize = 512;
            expect (! settings.getSyncMeasurement (SyncKey::from (other)).has_value());
        }
    };

    static SyncTests syncTests;
}
