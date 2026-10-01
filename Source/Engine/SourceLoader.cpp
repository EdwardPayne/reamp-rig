#include "SourceLoader.h"

#include <cmath>

namespace rf::engine
{
    namespace
    {
        constexpr int chunkSize = 1 << 16;

        bool aborted (const std::function<bool()>& shouldAbort)
        {
            return shouldAbort != nullptr && shouldAbort();
        }

        /** Resamples `in` from `fromRate` to `toRate` with a windowed sinc interpolator,
            removing the interpolator's latency so sample 0 stays at time 0. */
        bool resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate,
                       juce::AudioBuffer<float>& out, const std::function<bool()>& shouldAbort)
        {
            const auto ratio = fromRate / toRate;   // input samples per output sample
            const auto outLength = (juce::int64) std::ceil ((double) in.getNumSamples() / ratio);
            const auto latency = (int) std::lround ((double) juce::WindowedSincInterpolator::getBaseLatency() / ratio);

            if (outLength + latency > std::numeric_limits<int>::max())
                return false;

            out.setSize (1, (int) outLength, false, false, false);

            juce::WindowedSincInterpolator interpolator;
            juce::HeapBlock<float> block ((size_t) chunkSize);

            const auto* src = in.getReadPointer (0);
            const auto available = in.getNumSamples();
            auto inPos = 0;
            juce::int64 produced = 0;   // including the latency samples that are dropped
            const auto total = outLength + latency;

            while (produced < total)
            {
                if (aborted (shouldAbort))
                    return false;

                const auto n = (int) juce::jmin ((juce::int64) chunkSize, total - produced);
                inPos += interpolator.process (ratio, src + juce::jmin (inPos, available), block.get(), n,
                                               juce::jmax (0, available - inPos), 0);

                for (int i = 0; i < n; ++i)
                {
                    const auto outIndex = produced + i - latency;

                    if (outIndex >= 0)
                        out.setSample (0, (int) outIndex, block[i]);
                }

                produced += n;
            }

            return true;
        }
    }

    std::shared_ptr<const LoadedSource> SourceLoader::load (const LoadRequest& request, juce::AudioFormatManager& formats,
                                                            juce::String& error, const std::function<bool()>& shouldAbort)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (request.file));

        if (reader == nullptr)
        {
            error = "not a readable audio file";
            return {};
        }

        const auto numChannels = (int) reader->numChannels;
        const auto length = reader->lengthInSamples;

        if (numChannels <= 0 || reader->sampleRate <= 0.0)
        {
            error = "the file has no audio";
            return {};
        }

        if (length > std::numeric_limits<int>::max() / 2)
        {
            error = "the file is too long to preview";
            return {};
        }

        auto source = std::make_shared<LoadedSource>();
        source->file = request.file;
        source->channel = juce::jlimit (0, numChannels - 1, request.channel);
        source->fileSampleRate = reader->sampleRate;
        source->fileLengthInSamples = length;

        juce::AudioBuffer<float> decoded (1, (int) length);
        decoded.clear();

        // Read only the chosen channel: every other destination pointer is null.
        std::vector<float*> destinations ((size_t) numChannels, nullptr);

        for (juce::int64 pos = 0; pos < length; pos += chunkSize)
        {
            if (aborted (shouldAbort))
            {
                error = "cancelled";
                return {};
            }

            const auto n = (int) juce::jmin ((juce::int64) chunkSize, length - pos);
            destinations[(size_t) source->channel] = decoded.getWritePointer (0, (int) pos);

            if (! reader->read (destinations.data(), numChannels, pos, n))
            {
                error = "could not decode the file";
                return {};
            }
        }

        const auto range = juce::FloatVectorOperations::findMinAndMax (decoded.getReadPointer (0), decoded.getNumSamples());
        source->peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));

        const auto target = request.targetSampleRate;

        if (target > 0.0 && ! juce::approximatelyEqual (target, reader->sampleRate))
        {
            if (! resample (decoded, reader->sampleRate, target, source->samples, shouldAbort))
            {
                error = aborted (shouldAbort) ? "cancelled" : "the file is too long to preview";
                return {};
            }

            source->sampleRate = target;
            source->resampled = true;
        }
        else
        {
            source->samples = std::move (decoded);
            source->sampleRate = reader->sampleRate;
        }

        return source;
    }

    //==============================================================================
    struct SourceLoader::Job final : public juce::ThreadPoolJob
    {
        Job (SourceLoader& l, LoadRequest r, juce::uint32 id, Callback cb)
            : ThreadPoolJob ("Source load"),
              loader (l), request (std::move (r)), requestId (id), onDone (std::move (cb)), alive (l.alive)
        {
        }

        JobStatus runJob() override
        {
            juce::String error;
            auto result = SourceLoader::load (request, loader.formats, error, [this] { return shouldExit(); });

            if (shouldExit())
                return jobHasFinished;

            juce::MessageManager::callAsync ([flag = alive, &owner = loader, id = requestId,
                                              callback = std::move (onDone), r = std::move (result), error]
            {
                if (! flag->load() || owner.pendingRequest != id)
                    return;   // destroyed, cancelled or superseded

                owner.pendingRequest = 0;

                if (callback != nullptr)
                    callback (r, error);
            });

            return jobHasFinished;
        }

        SourceLoader& loader;
        LoadRequest request;
        juce::uint32 requestId;
        Callback onDone;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    SourceLoader::SourceLoader()
    {
        formats.registerBasicFormats();
    }

    SourceLoader::~SourceLoader()
    {
        alive->store (false);
        pool.removeAllJobs (true, 5000);
    }

    void SourceLoader::loadAsync (LoadRequest request, Callback onDone)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        cancel();

        pendingRequest = ++lastRequest;
        pool.addJob (new Job (*this, std::move (request), pendingRequest, std::move (onDone)), true);
    }

    void SourceLoader::cancel()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        pendingRequest = 0;
        pool.removeAllJobs (true, 0);   // interrupts a running job; it checks shouldExit()
    }
}
