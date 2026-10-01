#include "DuplexEngine.h"

#include <algorithm>

namespace rf::engine
{
    namespace
    {
        /** Lock-free "store the maximum" for a peak shared with the message thread. */
        void storeMax (std::atomic<float>& target, float value) noexcept
        {
            auto previous = target.load (std::memory_order_relaxed);

            while (value > previous && ! target.compare_exchange_weak (previous, value, std::memory_order_relaxed))
            {
            }
        }

        float blockPeak (const float* data, int numSamples) noexcept
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (data, numSamples);
            return juce::jmax (-range.getStart(), range.getEnd());
        }
    }

    DuplexEngine::DuplexEngine() = default;

    DuplexEngine::~DuplexEngine()
    {
        // The owner detaches this callback from the device before destroying it, so no audio
        // thread can still be reading the commands freed here.
        jassert (! streamRunning.load());
    }

    //==============================================================================
    bool DuplexEngine::startAudition (std::shared_ptr<const LoadedSource> source, juce::int64 startSample)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (source == nullptr || source->getNumSamples() == 0
            || startSample < 0 || startSample >= source->getNumSamples())
            return false;

        stopAudition();

        auto next = std::make_unique<Command>();
        next->source = std::move (source);
        next->startSample = startSample;
        next->generation = nextGeneration++;

        playhead.store (startSample, std::memory_order_relaxed);
        command.store (next.get(), std::memory_order_release);
        current = std::move (next);
        return true;
    }

    void DuplexEngine::stopAudition()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (current == nullptr)
            return;

        command.store (nullptr, std::memory_order_release);
        retire (std::move (current));
    }

    void DuplexEngine::setGainDb (float db)
    {
        gainDb = db;
        targetGain.store (juce::Decibels::decibelsToGain (db, -1000.0f), std::memory_order_relaxed);
    }

    void DuplexEngine::retire (std::unique_ptr<Command> c)
    {
        // The audio thread may be inside a callback that loaded `c` before the store above.
        // Every callback that could have seen it has finished once the count moves past this.
        retired.emplace_back (std::move (c), callbackCount.load (std::memory_order_acquire));
        releaseRetired();
    }

    void DuplexEngine::releaseRetired()
    {
        const auto running = streamRunning.load (std::memory_order_acquire);
        const auto count = callbackCount.load (std::memory_order_acquire);

        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [&] (const auto& entry) { return ! running || count > entry.second; }),
                       retired.end());
    }

    EngineSnapshot DuplexEngine::poll()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        releaseRetired();

        EngineSnapshot s;
        s.running = streamRunning.load();
        s.auditioning = current != nullptr;
        s.auditionFinished = current != nullptr && finishedGeneration.load() == current->generation;
        s.playheadSample = playhead.load (std::memory_order_relaxed);
        s.sourceSampleRate = current != nullptr ? current->source->sampleRate : 0.0;
        s.inputPeak = inputPeak.exchange (0.0f, std::memory_order_relaxed);
        s.outputPeak = outputPeak.exchange (0.0f, std::memory_order_relaxed);
        s.inputClipped = inputClip.exchange (false, std::memory_order_relaxed);
        s.outputClipped = outputClip.exchange (false, std::memory_order_relaxed);
        return s;
    }

    //==============================================================================
    void DuplexEngine::streamStarting (const StreamLayout& newLayout)
    {
        layout = newLayout;
        inputIndex.store (newLayout.inputIndex);
        outputIndex.store (newLayout.outputIndex);
        streamRunning.store (true, std::memory_order_release);
    }

    void DuplexEngine::streamStopped()
    {
        // May be called off the message thread when a driver stops a device on its own, so
        // retired commands are freed on the next poll(), not here.
        streamRunning.store (false, std::memory_order_release);
    }

    void DuplexEngine::process (const float* const* inputs, int numInputs,
                                float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        // Every output channel starts silent; only the selected one is written below.
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);

        const auto inIndex = inputIndex.load (std::memory_order_relaxed);
        const auto outIndex = outputIndex.load (std::memory_order_relaxed);

        const float* in = juce::isPositiveAndBelow (inIndex, numInputs) ? inputs[inIndex] : nullptr;
        float* out = juce::isPositiveAndBelow (outIndex, numOutputs) ? outputs[outIndex] : nullptr;

        const auto* cmd = command.load (std::memory_order_acquire);

        if (cmd != active)
        {
            active = cmd;

            if (cmd != nullptr)
            {
                position = cmd->startSample;
                currentGain = targetGain.load (std::memory_order_relaxed);
            }
        }

        const auto target = targetGain.load (std::memory_order_relaxed);

        if (active != nullptr && out != nullptr)
        {
            const auto& samples = active->source->samples;
            const auto length = (juce::int64) samples.getNumSamples();
            const auto n = (int) juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, length - position);

            if (n > 0)
            {
                const auto* src = samples.getReadPointer (0, (int) position);

                if (juce::approximatelyEqual (currentGain, target))
                {
                    juce::FloatVectorOperations::multiply (out, src, target, n);
                }
                else
                {
                    // Linear ramp over this block to avoid a click when the level changes.
                    const auto delta = target - currentGain;
                    const auto scale = 1.0f / (float) n;

                    for (int i = 0; i < n; ++i)
                        out[i] = src[i] * (currentGain + delta * ((float) (i + 1) * scale));
                }

                position += n;
                playhead.store (position, std::memory_order_relaxed);
            }

            if (position >= length)
                finishedGeneration.store (active->generation, std::memory_order_relaxed);
        }

        currentGain = target;

        if (out != nullptr)
        {
            const auto peak = blockPeak (out, numSamples);
            storeMax (outputPeak, peak);

            if (peak >= clipLevel)
                outputClip.store (true, std::memory_order_relaxed);
        }

        if (in != nullptr)
        {
            const auto peak = blockPeak (in, numSamples);
            storeMax (inputPeak, peak);

            if (peak >= clipLevel)
                inputClip.store (true, std::memory_order_relaxed);
        }

        callbackCount.fetch_add (1, std::memory_order_acq_rel);
    }
}
