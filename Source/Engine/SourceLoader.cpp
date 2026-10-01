#include "SourceLoader.h"
#include "Resampler.h"

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

        /** Resamples `in` from `fromRate` to `toRate` with the record path's Resampler
            (aligned: output sample 0 is input sample 0, no latency). */
        bool resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate,
                       juce::AudioBuffer<float>& out, const std::function<bool()>& shouldAbort)
        {
            const auto outLength = Resampler::getOutputLength (in.getNumSamples(), fromRate, toRate);

            if (outLength > std::numeric_limits<int>::max() / 2)
                return false;

            out.setSize (1, (int) outLength, false, false, false);

            const Resampler resampler (fromRate, toRate);
            return resampler.process (in.getReadPointer (0), in.getNumSamples(), out.getWritePointer (0), 0, outLength,
                                      shouldAbort);
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
