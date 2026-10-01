#pragma once

#include "AudioDeviceInterface.h"

namespace rf::engine
{
    /*  Turning a wanted (saved or user-chosen) configuration into one that can be opened on
        the devices present right now, with plain-language warnings for every fallback.
        No hardware needed: the tests drive this with a fake AudioDeviceInterface.
    */

    /** Result of matching a wanted channel against the driver's channel names. */
    struct ChannelChoice
    {
        int index = -1;             // -1 when the device has no channels in that direction
        bool changed = false;       // true when the wanted channel could not be kept
    };

    /** Finds a channel by name first (drivers may reorder channels), then by index, else
        falls back to the first channel. An empty wanted name with index -1 means "no
        preference" and picks the first channel without reporting a change. */
    ChannelChoice resolveChannel (const juce::StringArray& names, const juce::String& wantedName, int wantedIndex);

    /** Index of `channel` in the packed per-callback channel arrays, given the set of open
        channels (JUCE passes only the open channels, in ascending order). -1 if not open. */
    int packedChannelIndex (const juce::BigInteger& openChannels, int channel);

    //==============================================================================
    /*  Opens devices with graceful fallback:

        1. driver type: the wanted one if present, else the first available;
        2. output/input device: the wanted one if present, else (for input) the device with the
           same name as the output, else the system default, else the first listed;
        3. opens it; sample rate and buffer size fall back to the nearest supported values;
        4. channels are matched by name, then index, else the first channel (reopening with the
           resolved channels if they differ from what was opened).

        Each fallback adds a warning. The input is only opened when `openInput` is true (the
        app passes false until microphone access is granted: on macOS even creating a device
        with an input blocks until the permission prompt is answered). Without it the wanted
        input channel is returned unchanged, since its names cannot be read.
    */
    class DeviceSession
    {
    public:
        struct Result
        {
            bool ok = false;
            DeviceConfig config;        // the resolved choice (input channel even if not opened)
            bool inputOpen = false;
            juce::StringArray warnings;
            juce::String error;
        };

        explicit DeviceSession (AudioDeviceInterface& d) : device (d) {}

        /** Resolves the type and device names only (no open). */
        DeviceConfig resolveDevices (const DeviceConfig& wanted, juce::StringArray& warnings);

        Result open (const DeviceConfig& wanted, bool openInput);

        /** True when the driver type and every device named in `wanted` (output, input; empty
            names are ignored) are listed right now. False when `wanted` names no device. */
        bool isPresent (const DeviceConfig& wanted);

        /** True when `status` is open on the driver type and devices named in `wanted`. */
        static bool runs (const DeviceStatus& status, const DeviceConfig& wanted);

    private:
        AudioDeviceInterface& device;
    };

    //==============================================================================
    /*  Decides when the saved device should be reopened by itself (phase 6): when it was
        missing (unplugged while open, or absent at launch so another device was opened) and
        is listed again. Fed with the presence of the wanted devices on every device-list
        change and on a slow poll. A comeback that arrives while the device cannot be
        reopened (a take or a sync measurement runs) is remembered until it can.
    */
    class ReconnectWatch
    {
    public:
        /** Starts watching; `presentNow` is the wanted devices' presence at this moment. */
        void reset (bool presentNow) noexcept       { wasPresent = presentNow; pending = false; }

        /** Returns true when the wanted devices should be reopened now. */
        bool update (bool presentNow, bool runningWanted, bool canReopenNow) noexcept
        {
            if (! presentNow)
            {
                wasPresent = false;
                pending = false;
                return false;
            }

            if (! wasPresent)
                pending = true;      // it has come back

            wasPresent = true;

            if (runningWanted)
                pending = false;     // already running (e.g. the user picked it again)

            if (pending && canReopenNow)
            {
                pending = false;
                return true;
            }

            return false;
        }

        bool isPending() const noexcept             { return pending; }

    private:
        bool wasPresent = true, pending = false;
    };
}
