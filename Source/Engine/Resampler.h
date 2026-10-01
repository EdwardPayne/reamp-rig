#pragma once

#include <juce_core/juce_core.h>

#include <functional>

#include <memory>
#include <vector>

namespace rf::engine
{
    /*  High-quality sample-rate conversion for the record path (PROMPT.md section 4.4).

        A Kaiser-windowed sinc (64 zero crossings per side, beta 10, cutoff at 0.47 of the
        lower of the two rates) is evaluated at the exact position of every output sample:
        output sample m is the input signal at time m / outputRate, so output sample 0 is input
        sample 0. There is no latency and no fractional offset to compensate, which is what
        lets a resampled take come back sample-aligned with the source. Positions are computed
        with exact integer arithmetic for integer rates, so the alignment does not drift over
        long files. Input outside the given range counts as silence.

        Measured error (Tests/ResamplerTests.cpp, ARCHITECTURE.md): a faded 1 kHz sine at
        -6 dBFS converted 44.1 -> 48, 48 -> 44.1, 44.1 -> 96 or 96 -> 48 kHz deviates from the
        ideal sine at the new rate by at most -122 dBFS (peak), and a 44.1 -> 48 -> 44.1 kHz
        round trip by -116 dBFS; the record path requires better than -80 dB. The passband
        reaches about 0.447 of the lower rate (19.7 kHz at 44.1 kHz); content above that is
        attenuated (stopband from 0.4935 of the lower rate, about -100 dB).

        Thread-agnostic. The kernel table is built once and shared (read-only).
    */
    class Resampler
    {
    public:
        Resampler (double inputRate, double outputRate);

        double getInputRate() const noexcept    { return inRate; }
        double getOutputRate() const noexcept   { return outRate; }

        /** Input samples needed on each side of an output position. */
        int getHalfWidth() const noexcept       { return halfWidth; }

        /** ceil (inputLength * outputRate / inputRate): output samples covering the input. */
        static juce::int64 getOutputLength (juce::int64 inputLength, double inputRate, double outputRate);

        /** Offline conversion: writes output samples [firstOutput, firstOutput + numOutput)
            from the complete input `in` of `inLength` samples. `shouldAbort` (optional) is
            polled every few thousand samples; returns false if it aborted. */
        bool process (const float* in, juce::int64 inLength, float* out, juce::int64 firstOutput,
                      juce::int64 numOutput, const std::function<bool()>& shouldAbort = {}) const;

        /** Position of output sample `m` in input samples: integer part and fraction. */
        void getInputPosition (juce::int64 m, juce::int64& integer, double& fraction) const noexcept;

    private:
        friend class ResamplerStream;
        struct Kernel;

        float evaluate (const float* in, juce::int64 inStart, juce::int64 inEnd, juce::int64 integer, double fraction) const noexcept;

        double inRate, outRate;
        juce::int64 inStep = 0, outStep = 0;    // exact rational ratio for integer rates (0 otherwise)
        double ratio;                           // input samples per output sample
        double scale;                           // 2 * cutoff, in cycles per input sample * 2
        int halfWidth;
        std::shared_ptr<const Kernel> kernel;
    };

    //==============================================================================
    /** Streaming conversion with the same filter (the writer thread): push input in order,
        pull output as soon as enough input has arrived; finish() treats everything after the
        input as silence. Not real-time safe (it grows a buffer); used off the audio thread. */
    class ResamplerStream
    {
    public:
        ResamplerStream (double inputRate, double outputRate);

        void push (const float* data, int numSamples);
        void finish() noexcept                      { finished = true; }

        /** Writes up to `maxSamples` output samples to `dest`; returns how many. */
        int pull (float* dest, int maxSamples);

        juce::int64 getNumOutput() const noexcept   { return nextOutput; }

    private:
        Resampler resampler;
        std::vector<float> buffer;      // input samples from `bufferStart` on
        juce::int64 bufferStart = 0;    // absolute index of buffer[0]
        juce::int64 numInput = 0;       // absolute count pushed
        juce::int64 nextOutput = 0;
        bool finished = false;
    };
}
