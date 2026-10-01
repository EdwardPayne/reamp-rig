#include "SyncMeasurer.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace rf::engine
{
    namespace
    {
        float toDb (float gain)     { return juce::Decibels::gainToDecibels (gain, -120.0f); }

        juce::String dbText (float db)
        {
            return juce::String (db, 1) + " dBFS";
        }

        /** Lower median of a copy. */
        template <typename T>
        T median (std::vector<T> values)
        {
            jassert (! values.empty());
            std::sort (values.begin(), values.end());
            return values[(values.size() - 1) / 2];
        }
    }

    //==============================================================================
    /*  Drains one repeat's RecordStream into memory and analyses it, off the message
        thread. One job at a time; the thread lives for one measurement. */
    class SyncMeasurer::Worker final : private juce::Thread
    {
    public:
        Worker (std::shared_ptr<const std::vector<float>> s, double r)
            : juce::Thread ("Sync analysis"), signal (std::move (s)), rate (r)
        {
            startThread();
        }

        ~Worker() override
        {
            stopThread (4000);
        }

        void submit (std::shared_ptr<RecordStream> newStream, int length)
        {
            {
                const std::scoped_lock lock (mutex);
                pending = Job { std::move (newStream), length };
                result.reset();
            }

            notify();
        }

        std::optional<SyncRepeat> takeResult()
        {
            const std::scoped_lock lock (mutex);
            auto r = std::move (result);
            result.reset();
            return r;
        }

    private:
        struct Job
        {
            std::shared_ptr<RecordStream> stream;
            int length = 0;
        };

        void run() override
        {
            std::vector<float> recording;

            while (! threadShouldExit())
            {
                Job job;

                {
                    const std::scoped_lock lock (mutex);

                    if (pending.has_value())
                    {
                        job = std::move (*pending);
                        pending.reset();
                    }
                }

                if (job.stream == nullptr)
                {
                    wait (5);
                    continue;
                }

                recording.assign ((size_t) job.length, 0.0f);
                int got = 0;

                // The audio thread never signals: poll the FIFO (as the FileWriter does).
                while (! threadShouldExit() && got < job.length)
                {
                    const auto n = job.stream->read (recording.data() + got, job.length - got);
                    got += n;

                    if (n == 0)
                    {
                        if (job.stream->isComplete() && job.stream->getNumReady() == 0)
                            break;

                        wait (2);
                    }
                }

                if (threadShouldExit())
                    break;

                auto analysis = analyse (recording.data(), got, *signal, rate);

                // Samples lost to the FIFO, a late callback or a stream restart make the loop's
                // timing unknown.
                if (got < job.length || job.stream->getNumDropped() > 0 || job.stream->getCallbackGaps() > 0
                    || job.stream->getRestarts() > 0)
                    analysis.outcome = SyncRepeat::Outcome::dropout;

                const std::scoped_lock lock (mutex);
                result = analysis;
            }
        }

        const std::shared_ptr<const std::vector<float>> signal;
        const double rate;

        std::mutex mutex;           // message thread <-> worker, never the audio thread
        std::optional<Job> pending;
        std::optional<SyncRepeat> result;
    };

    //==============================================================================
    SyncMeasurer::SyncMeasurer() = default;

    SyncMeasurer::~SyncMeasurer()
    {
        if (isRunning())
            cancel();
    }

    juce::String SyncMeasurer::start (const SyncOptions& newOptions, double deviceRate, DuplexEngine& e)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        jassert (! isRunning());

        if (deviceRate <= 0.0)
            return "no audio device is running";

        options = newOptions;
        options.repeats = juce::jlimit (minAgreeing, 15, options.repeats);
        options.levelDb = juce::jlimit (-90.0f, 0.0f, options.levelDb);
        rate = deviceRate;
        engine = &e;

        signal = std::make_shared<const std::vector<float>> (makeTestSignal (rate, options.levelDb));
        const auto m = (juce::int64) signal->size();

        auto loaded = std::make_shared<LoadedSource>();
        loaded->fileSampleRate = loaded->sampleRate = rate;
        loaded->fileLengthInSamples = m;
        loaded->samples.setSize (1, (int) m);
        loaded->samples.copyFrom (0, 0, signal->data(), (int) m);
        loaded->peak = juce::Decibels::decibelsToGain (options.levelDb);
        source = std::move (loaded);

        // About a second, and room for twice the driver's estimate if that is longer.
        recordLength = juce::jmax ((juce::int64) std::llround (options.recordSeconds * rate),
                                   m + 2 * (juce::int64) juce::jmax (0, options.latencyHint) + (juce::int64) std::llround (0.1 * rate));

        repeats.clear();
        result = {};
        result.sampleRate = rate;

        // The level is absolute: the output level control does not apply to the test signal.
        savedGainDb = engine->getGainDb();
        engine->setGainDb (0.0f);

        worker = std::make_unique<Worker> (signal, rate);

        if (const auto error = startRepeat(); error.isNotEmpty())
        {
            worker.reset();
            engine->setGainDb (savedGainDb);
            phase = Phase::idle;
            return error;
        }

        return {};
    }

    juce::String SyncMeasurer::startRepeat()
    {
        auto recordStream = std::make_shared<RecordStream> ((int) recordLength + 16);   // the whole repeat fits
        stream = recordStream.get();
        worker->submit (recordStream, (int) recordLength);

        if (! engine->startTake (source, recordLength, recordStream))
            return "the engine refused the measurement";

        phase = Phase::measuring;
        position = 0;
        lastProgressMs = juce::Time::getMillisecondCounterHiRes();
        return {};
    }

    SyncMeasurer::Phase SyncMeasurer::update (const EngineSnapshot& snap)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (phase == Phase::measuring)
        {
            if (snap.taking && snap.takeStream == stream)
            {
                if (snap.takePosition != position)
                {
                    position = snap.takePosition;
                    lastProgressMs = now;
                }

                if (snap.takeFinished)
                {
                    engine->stopTake();
                    position = recordLength;
                    phase = Phase::analysing;
                }
            }

            if (phase == Phase::measuring && now - lastProgressMs > stallSeconds * 1000.0)
            {
                SyncResult r;
                r.failure = SyncFailure::deviceStopped;
                r.summary = "the device stopped";
                r.message = "The audio device stopped delivering audio during the measurement. Check the device and try again.";
                finish (std::move (r));
                return phase;
            }
        }

        if (phase == Phase::analysing)
        {
            if (auto repeat = worker->takeResult())
            {
                repeats.push_back (*repeat);

                const auto failed = (int) std::count_if (repeats.begin(), repeats.end(),
                                                         [] (const SyncRepeat& r) { return r.outcome != SyncRepeat::Outcome::ok; });
                const auto clipped = repeat->outcome == SyncRepeat::Outcome::clipped;
                const auto hopeless = failed > options.repeats - minAgreeing;

                if (clipped || hopeless || (int) repeats.size() >= options.repeats)
                {
                    finish (combine (repeats, options.repeats, rate));
                }
                else if (const auto error = startRepeat(); error.isNotEmpty())
                {
                    SyncResult r;
                    r.failure = SyncFailure::deviceStopped;
                    r.summary = "could not start";
                    r.message = "The measurement could not continue: " + error + ".";
                    finish (std::move (r));
                }
            }
        }

        return phase;
    }

    void SyncMeasurer::cancel()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (! isRunning())
            return;

        SyncResult r;
        r.failure = SyncFailure::cancelled;
        r.summary = "cancelled";
        r.message = "Sync cancelled. Nothing was stored.";
        finish (std::move (r));
    }

    void SyncMeasurer::finish (SyncResult r)
    {
        if (engine != nullptr)
        {
            engine->stopTake();
            engine->setGainDb (savedGainDb);
        }

        worker.reset();     // stops the thread (it holds the last stream)
        stream = nullptr;

        r.sampleRate = rate;

        if (r.repeats.empty())
            r.repeats = repeats;

        result = std::move (r);
        phase = Phase::done;
    }

    double SyncMeasurer::getProgress() const noexcept
    {
        if (phase == Phase::done)
            return 1.0;

        if (phase == Phase::idle || options.repeats <= 0)
            return 0.0;

        const auto current = recordLength > 0 ? juce::jlimit (0.0, 1.0, (double) position / (double) recordLength) : 0.0;
        return juce::jlimit (0.0, 1.0, ((double) repeats.size() + current) / (double) options.repeats);
    }

    //==============================================================================
    std::vector<float> SyncMeasurer::makeTestSignal (double sampleRate, float levelDb)
    {
        const auto level = juce::Decibels::decibelsToGain (levelDb);
        const auto gap = (int) std::lround (clickGapSeconds * sampleRate);
        const auto sweepLength = (int) std::lround (sweepSeconds * sampleRate);
        const auto fade = juce::jmax (1, (int) std::lround (fadeSeconds * sampleRate));

        std::vector<float> s ((size_t) (1 + gap + sweepLength), 0.0f);
        s[0] = level;   // the click

        const auto f1 = sweepStartHz;
        const auto f2 = juce::jmin (sweepEndHz, 0.45 * sampleRate);
        const auto duration = (double) sweepLength / sampleRate;
        const auto logRatio = std::log (f2 / f1);
        const auto k = juce::MathConstants<double>::twoPi * f1 * duration / logRatio;

        for (int i = 0; i < sweepLength; ++i)
        {
            const auto t = (double) i / sampleRate;
            auto env = 1.0;

            if (i < fade)
                env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) i / fade);
            else if (i > sweepLength - 1 - fade)
                env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) (sweepLength - 1 - i) / fade);

            const auto phase = k * (std::exp (t / duration * logRatio) - 1.0);
            s[(size_t) (1 + gap + i)] = (float) (level * env * std::sin (phase));
        }

        return s;
    }

    int SyncMeasurer::getExclusionSamples (double sampleRate)
    {
        return (int) std::ceil (2.0 * sampleRate / sweepStartHz);
    }

    SyncRepeat SyncMeasurer::analyse (const float* recording, int n, const std::vector<float>& signal, double sampleRate)
    {
        SyncRepeat r;
        const auto m = (int) signal.size();

        if (n <= 0 || m <= 0)
        {
            r.outcome = SyncRepeat::Outcome::dropout;
            return r;
        }

        const auto range = juce::FloatVectorOperations::findMinAndMax (recording, n);
        r.recordingPeak = juce::jmax (-range.getStart(), range.getEnd());

        if (r.recordingPeak >= DuplexEngine::clipLevel)
        {
            r.outcome = SyncRepeat::Outcome::clipped;
            r.returnedPeak = r.recordingPeak;
            return r;
        }

        if (toDb (r.recordingPeak) < silenceDb || n < m)
        {
            r.outcome = SyncRepeat::Outcome::silent;
            return r;
        }

        // Cross-correlation by FFT: corr[k] = sum_i recording[i + k] * signal[i], for every lag
        // with the whole signal inside the recording (0 .. n - m). Zero-padded to at least
        // n + m so the circular correlation does not wrap.
        const auto order = juce::jmax (4, (int) std::ceil (std::log2 ((double) (n + m))));
        juce::dsp::FFT fft (order);
        const auto size = fft.getSize();

        std::vector<float> a ((size_t) size * 2, 0.0f), b ((size_t) size * 2, 0.0f);
        std::copy (recording, recording + n, a.begin());
        std::copy (signal.begin(), signal.end(), b.begin());

        fft.performRealOnlyForwardTransform (a.data(), true);
        fft.performRealOnlyForwardTransform (b.data(), true);

        for (int bin = 0; bin <= size / 2; ++bin)
        {
            const auto ar = a[(size_t) (2 * bin)], ai = a[(size_t) (2 * bin + 1)];
            const auto br = b[(size_t) (2 * bin)], bi = b[(size_t) (2 * bin + 1)];
            a[(size_t) (2 * bin)]     = ar * br + ai * bi;     // a * conj (b)
            a[(size_t) (2 * bin + 1)] = ai * br - ar * bi;
        }

        fft.performRealOnlyInverseTransform (a.data());

        const auto lastLag = n - m;
        int best = 0;
        auto bestValue = 0.0f;

        for (int lag = 0; lag <= lastLag; ++lag)
        {
            const auto v = std::abs (a[(size_t) lag]);

            if (v > bestValue)
            {
                bestValue = v;
                best = lag;
            }
        }

        const auto exclusion = getExclusionSamples (sampleRate);
        auto sidelobe = 0.0f;

        for (int lag = 0; lag <= lastLag; ++lag)
            if (std::abs (lag - best) > exclusion)
                sidelobe = juce::jmax (sidelobe, std::abs (a[(size_t) lag]));

        r.delay = best;
        r.inverted = a[(size_t) best] < 0.0f;
        r.peakToSidelobeDb = bestValue <= 0.0f ? 0.0
                           : sidelobe <= bestValue * 1.0e-6f ? 120.0
                                                            : 20.0 * std::log10 ((double) bestValue / (double) sidelobe);

        const auto window = juce::FloatVectorOperations::findMinAndMax (recording + best, juce::jmin (m, n - best));
        r.returnedPeak = juce::jmax (-window.getStart(), window.getEnd());

        if (bestValue <= 0.0f || r.peakToSidelobeDb < minPeakToSidelobeDb)
            r.outcome = SyncRepeat::Outcome::noPeak;
        else if (toDb (r.returnedPeak) < minReturnedDb)
            r.outcome = SyncRepeat::Outcome::tooLow;
        else
            r.outcome = SyncRepeat::Outcome::ok;

        return r;
    }

    juce::String SyncMeasurer::describe (SyncRepeat::Outcome o)
    {
        switch (o)
        {
            case SyncRepeat::Outcome::ok:      return "ok";
            case SyncRepeat::Outcome::silent:  return "nothing came back";
            case SyncRepeat::Outcome::noPeak:  return "no clear peak";
            case SyncRepeat::Outcome::tooLow:  return "too quiet";
            case SyncRepeat::Outcome::clipped: return "clipped";
            case SyncRepeat::Outcome::dropout: return "dropout";
        }

        return {};
    }

    SyncResult SyncMeasurer::combine (const std::vector<SyncRepeat>& all, int requested, double sampleRate)
    {
        using Outcome = SyncRepeat::Outcome;

        SyncResult r;
        r.repeats = all;
        r.sampleRate = sampleRate;

        // Clipping anywhere spoils the measurement: the returned shape is not the signal.
        for (const auto& rep : all)
        {
            if (rep.outcome == Outcome::clipped)
            {
                r.failure = SyncFailure::clipped;
                r.summary = "clipped";
                r.message = "The returned signal clipped (it reached 0 dBFS). Lower the sync level or the interface's "
                            "input gain and press Sync again.";
                return r;
            }
        }

        std::vector<const SyncRepeat*> valid;

        for (const auto& rep : all)
            if (rep.outcome == Outcome::ok)
                valid.push_back (&rep);

        if ((int) valid.size() < minAgreeing)
        {
            // Report the most frequent problem (ties: the first in this order).
            std::map<Outcome, int> counts;

            for (const auto& rep : all)
                if (rep.outcome != Outcome::ok)
                    ++counts[rep.outcome];

            auto worst = Outcome::silent;
            auto worstCount = -1;

            for (auto o : { Outcome::silent, Outcome::noPeak, Outcome::tooLow, Outcome::dropout })
            {
                if (counts[o] > worstCount)
                {
                    worst = o;
                    worstCount = counts[o];
                }
            }

            auto loudest = 0.0f;
            auto bestRatio = 0.0;

            for (const auto& rep : all)
            {
                if (rep.outcome == Outcome::tooLow)
                    loudest = juce::jmax (loudest, rep.returnedPeak);

                if (rep.outcome == Outcome::noPeak)
                    bestRatio = juce::jmax (bestRatio, rep.peakToSidelobeDb);
            }

            switch (worst)
            {
                case Outcome::silent:
                    r.failure = SyncFailure::silent;
                    r.summary = "nothing came back";
                    r.message = "Nothing came back: the input stayed silent. Connect the output directly to the input "
                                "(bypass the amp), check the output and input channels and the input gain, then press Sync again.";
                    break;

                case Outcome::noPeak:
                    r.failure = SyncFailure::noPeak;
                    r.summary = "no clear peak";
                    r.message = "No clear peak: the test signal was not found in what came back (peak-to-sidelobe "
                                + juce::String (bestRatio, 1) + " dB, at least " + juce::String (minPeakToSidelobeDb, 0)
                                + " dB needed). Check that the output is cabled to the selected input and that nothing else "
                                  "is playing.";
                    break;

                case Outcome::tooLow:
                    r.failure = SyncFailure::tooLow;
                    r.summary = "level too low";
                    r.message = "The test signal came back too quietly (" + dbText (toDb (loudest)) + ", at least "
                                + dbText (minReturnedDb) + " needed). Raise the sync level or the interface's input gain.";
                    break;

                case Outcome::dropout:
                case Outcome::ok:
                case Outcome::clipped:
                    r.failure = SyncFailure::dropout;
                    r.summary = "dropouts";
                    r.message = "Dropouts during the measurement: audio was lost, so the timing cannot be trusted. "
                                "Try a larger buffer size and press Sync again.";
                    break;
            }

            return r;
        }

        // Outliers: further than `tolerance` from the median of the valid repeats.
        std::vector<int> delays;

        for (const auto* rep : valid)
            delays.push_back (rep->delay);

        const auto centre = median (delays);
        std::vector<const SyncRepeat*> kept;

        for (const auto* rep : valid)
            if (std::abs (rep->delay - centre) <= tolerance)
                kept.push_back (rep);

        if ((int) kept.size() < minAgreeing)
        {
            juce::StringArray values;

            for (auto d : delays)
                values.add (juce::String (d));

            r.failure = SyncFailure::unstable;
            r.summary = "not repeatable";
            r.message = "The round trip changed between repeats (" + values.joinIntoString (", ")
                        + " samples), so it cannot be trusted. Use one interface for output and input (separate "
                          "devices drift), avoid dropouts and press Sync again.";
            return r;
        }

        std::vector<int> keptDelays;
        std::vector<double> ratios;
        std::vector<float> peaks;
        auto inverted = 0;

        for (const auto* rep : kept)
        {
            keptDelays.push_back (rep->delay);
            ratios.push_back (rep->peakToSidelobeDb);
            peaks.push_back (rep->returnedPeak);
            inverted += rep->inverted ? 1 : 0;
        }

        auto& m = r.measurement;
        m.samples = median (keptDelays);
        m.ms = (double) m.samples * 1000.0 / sampleRate;
        m.returnedPeakDb = toDb (median (peaks));
        m.peakToSidelobeDb = median (ratios);
        m.repeatsUsed = (int) kept.size();
        m.repeatsTotal = requested;
        m.date = juce::Time::getCurrentTime();

        const auto allAgree = (int) kept.size() == requested;
        const auto mostAgree = (int) kept.size() >= requested - 1;

        m.confidence = allAgree && m.peakToSidelobeDb >= highPeakToSidelobeDb    ? SyncConfidence::high
                     : mostAgree && m.peakToSidelobeDb >= mediumPeakToSidelobeDb ? SyncConfidence::medium
                                                                                 : SyncConfidence::low;

        r.ok = true;
        r.inverted = inverted * 2 > (int) kept.size();
        r.summary = "ok";
        return r;
    }
}
