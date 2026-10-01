#include "Take.h"
#include "Resampler.h"

namespace rf::engine
{
    namespace
    {
        bool isResampled (const TakeSpec& s)
        {
            return ! juce::approximatelyEqual (s.deviceRate, s.source->fileSampleRate);
        }
    }

    Take::~Take()
    {
        if (phase == Phase::recording || phase == Phase::writing)
            cancel();
    }

    juce::int64 Take::getRecordLength (const TakeSpec& s)
    {
        const auto played = (juce::int64) s.source->getNumSamples();

        if (! isResampled (s))
            return played + s.latencySamples + s.tailSamples;

        const auto fileRate = s.source->fileSampleRate;
        return played + s.latencySamples
             + Resampler::getOutputLength (s.tailSamples, fileRate, s.deviceRate)
             + Resampler (s.deviceRate, fileRate).getHalfWidth() + 2;
    }

    juce::int64 Take::getOutputLength (const TakeSpec& s)
    {
        return s.source->fileLengthInSamples + s.tailSamples;
    }

    juce::String Take::start (TakeSpec newSpec, DuplexEngine& e, FileWriter& w)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        jassert (phase == Phase::idle);

        if (newSpec.source == nullptr || newSpec.source->getNumSamples() == 0)
            return "the source is empty";

        if (newSpec.deviceRate <= 0.0 || ! juce::approximatelyEqual (newSpec.deviceRate, newSpec.source->sampleRate))
            return "the source was not prepared at the device's sample rate";

        if (newSpec.latencySamples < 0 || newSpec.tailSamples < 0)
            return "invalid latency or tail";

        spec = std::move (newSpec);
        engine = &e;
        writer = &w;
        recordLength = getRecordLength (spec);
        result = {};
        result.file = spec.outputFile;
        result.resampled = isResampled (spec);
        result.notCalibrated = ! spec.latencyMeasured;

        const auto capacity = (int) juce::jlimit ((juce::int64) 1 << 14, (juce::int64) 1 << 24,
                                                  (juce::int64) (spec.fifoSeconds * spec.deviceRate));
        auto recordStream = std::make_shared<RecordStream> (capacity);
        stream = recordStream.get();

        WriteJob job;
        job.stream = recordStream;
        job.discardSamples = spec.latencySamples;
        job.outputLength = getOutputLength (spec);
        job.deviceRate = spec.deviceRate;
        job.fileRate = spec.source->fileSampleRate;
        job.file = spec.outputFile;
        job.bitsPerSample = spec.bitsPerSample;
        job.replaceExisting = spec.replaceExisting;
        job.onWritten = spec.onWritten;

        if (const auto error = writer->start (std::move (job)); error.isNotEmpty())
        {
            result.writeFailed = true;      // the destination cannot take the file
            return error;
        }

        if (! engine->startTake (spec.source, recordLength, recordStream))
        {
            writer->cancel();
            return "the engine refused the take";
        }

        phase = Phase::recording;
        position = 0;
        return {};
    }

    Take::Phase Take::update (const EngineSnapshot& snap)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (phase == Phase::recording)
        {
            if (! snap.taking || snap.takeStream != stream)
                return phase;   // a snapshot from before this take (or of another one)

            position = snap.takePosition;
            result.droppedSamples = snap.takeDropped;
            result.callbackGaps = snap.takeGaps;
            result.streamRestarts = snap.takeRestarts;

            if (snap.takeConfigChanged)
            {
                // The device came back at another sample rate or buffer size: the round trip
                // (and maybe the rate) differ from what this take compensates. Discard it.
                engine->stopTake();
                writer->cancel();
                result.ok = false;
                result.cancelled = false;
                result.deviceChanged = true;
                result.error = "the audio device restarted with another sample rate or buffer size during the take, "
                               "so the recording would be misaligned; it was discarded";
                phase = Phase::done;
                return phase;
            }

            if (snap.takeFinished)
            {
                // Everything is in the FIFO: free the engine; the writer drains the rest.
                engine->stopTake();
                position = recordLength;
                phase = Phase::writing;
            }
        }

        if (phase == Phase::writing)
        {
            if (const auto r = writer->getResult())
            {
                result.ok = r->ok;
                result.cancelled = r->cancelled;
                result.error = r->error;
                result.length = r->samplesWritten;
                result.peak = r->peak;
                result.paddedSamples = r->paddedSamples;
                result.writeFailed = r->writeFailed;
                result.silent = r->peak < silenceThreshold;
                result.clipped = r->peak >= DuplexEngine::clipLevel;
                phase = Phase::done;
            }
        }

        return phase;
    }

    void Take::cancel()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (phase == Phase::idle || phase == Phase::done)
            return;

        engine->stopTake();
        writer->cancel();       // waits for the writer thread; a finish() in progress completes

        // The writer may already have renamed the take to its final name (it finished between
        // two update() polls, or during cancel above). A cancelled take must not leave it.
        if (const auto r = writer->getResult(); r.has_value() && r->ok)
            r->file.deleteFile();

        result.cancelled = true;
        result.ok = false;
        result.error = "cancelled";
        phase = Phase::done;
    }

    double Take::getProgress() const noexcept
    {
        if (phase == Phase::done)
            return 1.0;

        if (recordLength <= 0)
            return 0.0;

        // Capturing is most of the work; writing out the last FIFO contents is quick.
        const auto captured = (double) position / (double) recordLength;
        return juce::jlimit (0.0, 1.0, phase == Phase::writing ? 0.99 : 0.98 * captured);
    }

    double Take::getPlayheadSeconds() const noexcept
    {
        if (spec.source == nullptr || spec.deviceRate <= 0.0)
            return 0.0;

        const auto played = juce::jmin (position, (juce::int64) spec.source->getNumSamples());
        return (double) played / spec.deviceRate;
    }
}
