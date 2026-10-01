#pragma once

#include "AudioDeviceInterface.h"

#include <optional>

namespace rf::engine
{
    /*  Keyed sync store types (PROMPT.md 3.6.4, 3.7): one measured round trip per device
        configuration. The key is the driver type, the input and output device, the sample
        rate and the buffer size; channels are not part of it (the latency is the device's).
        The App's Settings persists them; the batch looks one up for every take.
    */
    struct SyncKey
    {
        juce::String typeName;
        juce::String inputDevice;
        juce::String outputDevice;
        double sampleRate = 0.0;
        int bufferSize = 0;

        /** The configuration a device is running in (rate and buffer as running). */
        static SyncKey from (const DeviceStatus& status)
        {
            return from (status.config);
        }

        static SyncKey from (const DeviceConfig& c)
        {
            return { c.typeName, c.inputDevice, c.outputDevice, c.sampleRate, c.bufferSize };
        }

        SyncKey withSampleRate (double rate) const
        {
            auto k = *this;
            k.sampleRate = rate;
            return k;
        }

        bool isValid() const noexcept
        {
            return typeName.isNotEmpty() && (inputDevice.isNotEmpty() || outputDevice.isNotEmpty())
                && sampleRate > 0.0 && bufferSize > 0;
        }

        bool operator== (const SyncKey& o) const
        {
            return typeName == o.typeName && inputDevice == o.inputDevice && outputDevice == o.outputDevice
                && std::abs (sampleRate - o.sampleRate) < 0.5 && bufferSize == o.bufferSize;
        }

        bool operator!= (const SyncKey& o) const   { return ! operator== (o); }
    };

    enum class SyncConfidence { low, medium, high };

    inline juce::String toString (SyncConfidence c)
    {
        switch (c)
        {
            case SyncConfidence::high:   return "high";
            case SyncConfidence::medium: return "medium";
            case SyncConfidence::low:    break;
        }

        return "low";
    }

    inline std::optional<SyncConfidence> confidenceFromString (const juce::String& s)
    {
        for (auto c : { SyncConfidence::low, SyncConfidence::medium, SyncConfidence::high })
            if (s == toString (c))
                return c;

        return std::nullopt;
    }

    /** One stored measurement: the value of the keyed store. */
    struct SyncMeasurement
    {
        int samples = 0;                    // round trip in device samples (the take's latency)
        double ms = 0.0;                    // the same in milliseconds at the key's rate
        float returnedPeakDb = -100.0f;     // peak of the returned test signal, dBFS
        double peakToSidelobeDb = 0.0;      // median over the repeats used
        int repeatsUsed = 0;                // repeats within +-1 sample of the result
        int repeatsTotal = 0;
        SyncConfidence confidence = SyncConfidence::low;
        juce::Time date;                    // when it was measured (0 = unknown)

        bool operator== (const SyncMeasurement& o) const
        {
            return samples == o.samples && std::abs (ms - o.ms) < 1.0e-6
                && std::abs (returnedPeakDb - o.returnedPeakDb) < 0.01f
                && std::abs (peakToSidelobeDb - o.peakToSidelobeDb) < 0.01
                && repeatsUsed == o.repeatsUsed && repeatsTotal == o.repeatsTotal
                && confidence == o.confidence && date.toMilliseconds() / 1000 == o.date.toMilliseconds() / 1000;
        }
    };
}
