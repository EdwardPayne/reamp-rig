#pragma once

#include "../Engine/SyncMeasurement.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace rf::app::syncplan
{
    /*  Which sync measurement a batch needs and uses (PROMPT.md 3.6.4, 4.4). Pure functions,
        shared by the BatchController (Start warning, per-take lookup) and the tests.
    */

    /** The device rate a file is recorded at: its own rate when the device offers it (the
        batch switches to it), otherwise the device's current rate (the file is resampled). */
    inline double deviceRateFor (double fileRate, const engine::DeviceStatus& status)
    {
        for (auto r : status.sampleRates)
            if (std::abs (r - fileRate) < 0.5)
                return r;

        return status.config.sampleRate;
    }

    /** Every configuration a batch runs in: the current one first, then each other rate the
        batch switches to, in order of first use. */
    inline std::vector<engine::SyncKey> keysForBatch (const engine::DeviceStatus& status, const std::vector<double>& fileRates)
    {
        std::vector<engine::SyncKey> keys;
        const auto current = engine::SyncKey::from (status);
        keys.push_back (current);

        for (auto rate : fileRates)
        {
            const auto k = current.withSampleRate (deviceRateFor (rate, status));

            if (std::find (keys.begin(), keys.end(), k) == keys.end())
                keys.push_back (k);
        }

        return keys;
    }

    /** The keys without a stored measurement. */
    inline std::vector<engine::SyncKey> missing (const std::vector<engine::SyncKey>& keys,
                                                 const std::function<std::optional<engine::SyncMeasurement> (const engine::SyncKey&)>& lookup)
    {
        std::vector<engine::SyncKey> result;

        for (const auto& k : keys)
            if (! lookup (k).has_value())
                result.push_back (k);

        return result;
    }

    struct LatencyChoice
    {
        int samples = 0;
        bool measured = false;      // false: the driver-reported estimate ("not calibrated")
    };

    /** The latency for a take in `status`: the stored measurement for exactly that
        configuration, else the driver-reported input + output latency. */
    inline LatencyChoice chooseLatency (const engine::DeviceStatus& status, const std::optional<engine::SyncMeasurement>& stored)
    {
        if (stored.has_value() && stored->samples > 0)
            return { stored->samples, true };

        return { juce::jmax (0, status.inputLatencySamples + status.outputLatencySamples), false };
    }
}
