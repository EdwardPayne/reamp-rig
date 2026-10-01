#include "Engine/DeviceSession.h"
#include "FakeAudioDevice.h"

namespace rf::test
{
    using namespace rf::engine;

    namespace
    {
        DeviceConfig apolloConfig()
        {
            DeviceConfig c;
            c.typeName = "CoreAudio";
            c.inputDevice = c.outputDevice = "Apollo Twin";
            c.sampleRate = 48000.0;
            c.bufferSize = 256;
            c.inputChannel = 2;
            c.inputChannelName = "Hi-Z 1";
            c.outputChannel = 2;
            c.outputChannelName = "Line 3";
            return c;
        }

        bool anyContains (const juce::StringArray& lines, const juce::String& text)
        {
            for (const auto& l : lines)
                if (l.contains (text))
                    return true;

            return false;
        }
    }

    /*  Device selection logic without hardware: saved configuration restored, fallbacks when
        the saved device/type/channel/rate is gone, channel mapping by name and index,
        permission gating of the input, and failures that must not crash.
    */
    class DeviceSessionTests final : public juce::UnitTest
    {
    public:
        DeviceSessionTests() : juce::UnitTest ("DeviceSession", "DeviceSession") {}

        void runTest() override
        {
            beginTest ("resolveChannel: by name, then index, then first");
            {
                const juce::StringArray names { "Monitor L", "Monitor R", "Line 3", "Line 4" };

                auto c = resolveChannel (names, "Line 3", 0);
                expectEquals (c.index, 2);          // name wins over a stale index
                expect (! c.changed);

                c = resolveChannel (names, "Line 9", 1);
                expectEquals (c.index, 1);          // name gone: index kept, reported
                expect (c.changed);

                c = resolveChannel (names, "Line 9", 7);
                expectEquals (c.index, 0);
                expect (c.changed);

                c = resolveChannel (names, {}, 3);
                expectEquals (c.index, 3);
                expect (! c.changed);

                c = resolveChannel (names, {}, -1);  // no preference
                expectEquals (c.index, 0);
                expect (! c.changed);

                c = resolveChannel ({}, "Line 3", 2);
                expectEquals (c.index, -1);
                expect (c.changed);
            }

            beginTest ("packedChannelIndex maps device channels to callback buffers");
            {
                juce::BigInteger open;
                open.setBit (3);
                expectEquals (packedChannelIndex (open, 3), 0);    // only channel 3 open
                expectEquals (packedChannelIndex (open, 2), -1);
                expectEquals (packedChannelIndex (open, -1), -1);

                open.setBit (0);
                open.setBit (5);
                expectEquals (packedChannelIndex (open, 0), 0);
                expectEquals (packedChannelIndex (open, 3), 1);
                expectEquals (packedChannelIndex (open, 5), 2);
            }

            beginTest ("saved Apollo configuration is restored exactly");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);

                const auto r = session.open (apolloConfig(), true);
                expect (r.ok);
                expect (r.warnings.isEmpty(), r.warnings.joinIntoString ("; "));
                expect (r.config == apolloConfig());
                expect (r.inputOpen);
                expectEquals ((int) fake.openCalls.size(), 1);      // no reopen needed
                expect (fake.getStatus().isOpen);
                expect (! r.config.isSplit());
            }

            beginTest ("channels are found by name after the driver reorders them");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                fake.types[0].devices[2].outputs = { "Line 3", "Line 4", "Monitor L", "Monitor R" };
                DeviceSession session (fake);

                const auto r = session.open (apolloConfig(), true);
                expect (r.ok);
                expect (r.warnings.isEmpty(), r.warnings.joinIntoString ("; "));
                expectEquals (r.config.outputChannel, 0);
                expectEquals (r.config.outputChannelName, juce::String ("Line 3"));
                expectEquals (fake.getStatus().config.outputChannel, 0);    // reopened with the right channel
            }

            beginTest ("saved device missing: falls back to the defaults with a warning");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, false);   // Apollo not connected
                DeviceSession session (fake);

                const auto r = session.open (apolloConfig(), true);
                expect (r.ok);
                expectEquals (r.config.outputDevice, juce::String ("MacBook Pro Speakers"));
                expectEquals (r.config.inputDevice, juce::String ("MacBook Pro Microphone"));
                expect (r.config.isSplit());
                expect (anyContains (r.warnings, "Output device \"Apollo Twin\" not found"));
                expect (anyContains (r.warnings, "Input device \"Apollo Twin\" not found"));
                expect (anyContains (r.warnings, "Output channel \"Line 3\" not found"));
                expectEquals (r.config.outputChannel, 0);
                expectEquals (r.config.inputChannel, 0);
                expectEquals (r.config.inputChannelName, juce::String ("Input 1"));
                expect (fake.getStatus().isOpen);
            }

            beginTest ("first launch: defaults, first channels, no warnings");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);

                const auto r = session.open ({}, true);
                expect (r.ok);
                expect (r.warnings.isEmpty(), r.warnings.joinIntoString ("; "));
                expectEquals (r.config.typeName, juce::String ("CoreAudio"));
                expectEquals (r.config.outputDevice, juce::String ("MacBook Pro Speakers"));
                expectEquals (r.config.inputDevice, juce::String ("MacBook Pro Microphone"));
                expectEquals (r.config.outputChannel, 0);
                expectEquals (r.config.inputChannel, 0);
                expectEquals (r.config.sampleRate, 48000.0);
                expectEquals (r.config.bufferSize, 512);
            }

            beginTest ("a missing input device prefers the output device when it has inputs");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);

                auto wanted = apolloConfig();
                wanted.inputDevice = "USB Mic (unplugged)";

                const auto r = session.open (wanted, true);
                expect (r.ok);
                expectEquals (r.config.inputDevice, juce::String ("Apollo Twin"));
                expect (! r.config.isSplit());
                expect (anyContains (r.warnings, "USB Mic (unplugged)"));
                expectEquals (r.config.inputChannelName, juce::String ("Hi-Z 1"));
            }

            beginTest ("saved driver type missing: first available type");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);

                auto wanted = apolloConfig();
                wanted.typeName = "ASIO";

                const auto r = session.open (wanted, true);
                expect (r.ok);
                expectEquals (r.config.typeName, juce::String ("CoreAudio"));
                expect (anyContains (r.warnings, "Driver \"ASIO\" is not available"));
                expectEquals (r.config.outputDevice, juce::String ("Apollo Twin"));
            }

            beginTest ("unsupported sample rate and buffer size fall back to the nearest");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);

                auto wanted = apolloConfig();
                wanted.sampleRate = 88200.0;
                wanted.bufferSize = 300;

                const auto r = session.open (wanted, true);
                expect (r.ok);
                expectEquals (r.config.sampleRate, 96000.0);
                expectEquals (r.config.bufferSize, 256);
                expect (anyContains (r.warnings, "88.2 kHz is not available"));
                expect (anyContains (r.warnings, "Buffer size 300 is not available"));
            }

            beginTest ("input not opened without microphone access, but still resolved");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);

                const auto r = session.open (apolloConfig(), false);
                expect (r.ok);
                expect (! r.inputOpen);
                expectEquals (r.config.inputChannel, 2);            // shown in the UI
                expectEquals (fake.openCalls.back().inputChannel, -1);
                expectEquals (fake.getStatus().config.inputChannel, -1);
                expectEquals (fake.getStatus().config.outputChannel, 2);
            }

            beginTest ("a device that fails to open falls back to the defaults");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                fake.types[0].devices[2].failsToOpen = true;
                DeviceSession session (fake);

                const auto r = session.open (apolloConfig(), true);
                expect (r.ok);
                expectEquals (r.config.outputDevice, juce::String ("MacBook Pro Speakers"));
                expect (anyContains (r.warnings, "Could not open \"Apollo Twin\" (Device is busy)"));
            }

            // Review 2026-10-01, A1: a batch or sync rate switch must never land on the defaults.
            beginTest ("rate switch, no-fallback mode: a refused rate is an error, never the default devices");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                fake.types[0].devices[2].rejectedRates = { 44100.0 };
                DeviceSession session (fake);

                expect (session.open (apolloConfig(), true).ok);
                auto switched = apolloConfig();
                switched.sampleRate = 44100.0;

                // The normal path falls back to the system defaults (fine at launch) ...
                const auto loose = session.open (switched, true);
                expect (loose.ok);
                expectEquals (loose.config.outputDevice, juce::String ("MacBook Pro Speakers"));

                // ... the exact path does not.
                expect (session.open (apolloConfig(), true).ok);
                fake.openCalls.clear();

                const auto r = session.open (switched, true, DeviceSession::Mode::exact);
                expect (! r.ok);
                expect (r.error.contains ("\"Apollo Twin\""), r.error);
                expect (r.error.contains ("44.1 kHz"), r.error);
                expect (r.warnings.isEmpty());

                auto onlyApollo = ! fake.openCalls.empty();

                for (const auto& c : fake.openCalls)
                    onlyApollo = onlyApollo && c.outputDevice == "Apollo Twin" && c.inputDevice == "Apollo Twin";

                expect (onlyApollo, "no other device was opened");
                expect (! fake.getStatus().isOpen || fake.getStatus().config.outputDevice == "Apollo Twin");

                // The caller reopens the previous rate on the same device, exactly.
                const auto back = session.open (apolloConfig(), true, DeviceSession::Mode::exact);
                expect (back.ok, back.error);
                expectEquals (fake.getStatus().config.sampleRate, 48000.0);
                expectEquals (fake.getStatus().config.outputDevice, juce::String ("Apollo Twin"));
            }

            beginTest ("rate switch, no-fallback mode: success, a missing device, a rate the driver rounds");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);
                expect (session.open (apolloConfig(), true).ok);

                auto switched = apolloConfig();
                switched.sampleRate = 96000.0;
                const auto ok = session.open (switched, true, DeviceSession::Mode::exact);
                expect (ok.ok, ok.error);
                expectEquals (ok.config.sampleRate, 96000.0);
                expectEquals (ok.config.inputChannelName, juce::String ("Hi-Z 1"));
                expectEquals (ok.config.outputChannelName, juce::String ("Line 3"));

                // Not listed (unplugged a moment ago): an error, and the device is not touched.
                auto other = apolloConfig();
                other.inputDevice = other.outputDevice = "Apollo x8";
                fake.openCalls.clear();
                const auto missing = session.open (other, true, DeviceSession::Mode::exact);
                expect (! missing.ok);
                expect (missing.error.contains ("\"Apollo x8\" is not available"), missing.error);
                expect (fake.openCalls.empty(), "nothing was opened");
                expect (fake.getStatus().isOpen && fake.getStatus().config.outputDevice == "Apollo Twin");

                // A rate the device does not list: the driver would round it; exact refuses.
                auto odd = apolloConfig();
                odd.sampleRate = 88200.0;
                const auto rounded = session.open (odd, true, DeviceSession::Mode::exact);
                expect (! rounded.ok);
                expect (rounded.error.contains ("instead of 88.2 kHz"), rounded.error);
            }

            beginTest ("nothing opens: error, device closed, no crash");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, false);

                for (auto& d : fake.types[0].devices)
                    d.failsToOpen = true;

                DeviceSession session (fake);
                const auto r = session.open ({}, true);
                expect (! r.ok);
                expectEquals (r.error, juce::String ("Device is busy"));
                expect (! fake.getStatus().isOpen);
            }

            beginTest ("no drivers or no devices at all");
            {
                FakeAudioDevice none;
                DeviceSession session (none);
                auto r = session.open (apolloConfig(), true);
                expect (! r.ok);
                expect (r.error.isNotEmpty());

                FakeAudioDevice empty;
                empty.types.push_back ({ "CoreAudio", true, {}, {}, {} });
                DeviceSession emptySession (empty);
                r = emptySession.open (apolloConfig(), true);
                expect (! r.ok);
                expectEquals (r.error, juce::String ("No audio devices found"));
            }

            beginTest ("drivers without separate inputs (ASIO): the input follows the output");
            {
                FakeAudioDevice fake;
                FakeAudioDevice::Type asio;
                asio.name = "ASIO";
                asio.separateInputsAndOutputs = false;

                FakeAudioDevice::Device d;
                d.name = "Universal Audio Thunderbolt";
                d.inputs = { "Mic/Line 1", "Mic/Line 2" };
                d.outputs = { "Line 1", "Line 2", "Line 3" };
                asio.devices = { d };
                asio.defaultInput = asio.defaultOutput = d.name;
                fake.types.push_back (asio);

                DeviceSession session (fake);
                DeviceConfig wanted;
                wanted.typeName = "ASIO";
                wanted.outputDevice = d.name;
                wanted.inputDevice = "Something else";
                wanted.outputChannelName = "Line 3";

                const auto r = session.open (wanted, true);
                expect (r.ok);
                expectEquals (r.config.inputDevice, d.name);
                expectEquals (r.config.outputChannel, 2);
                expect (! r.config.isSplit());
            }

            beginTest ("the saved device comes back: presence, what runs, and when to reopen");
            {
                FakeAudioDevice fake;
                addStudioDevices (fake, true);
                DeviceSession session (fake);
                const auto apollo = apolloConfig();

                const auto apolloDevice = fake.types[0].devices.back();

                auto unplug = [&fake]
                {
                    auto& list = fake.types[0].devices;
                    list.erase (std::remove_if (list.begin(), list.end(), [] (const auto& d) { return d.name == "Apollo Twin"; }),
                                list.end());
                };

                auto plugIn = [&fake, apolloDevice] { fake.types[0].devices.push_back (apolloDevice); };

                expect (session.isPresent (apollo));
                expect (session.open (apollo, true).ok);
                expect (DeviceSession::runs (fake.getStatus(), apollo));

                // Split pair: both names must be listed.
                DeviceConfig split;
                split.typeName = "CoreAudio";
                split.outputDevice = "MacBook Pro Speakers";
                split.inputDevice = "MacBook Pro Microphone";
                expect (session.isPresent (split));
                expect (! DeviceSession::runs (fake.getStatus(), split));
                split.inputDevice = "USB Mic";
                expect (! session.isPresent (split));

                // Nothing named (first launch) is never "present": nothing to come back.
                DeviceConfig none;
                none.typeName = "CoreAudio";
                expect (! session.isPresent (none));

                // Another driver type is not the same device.
                auto asio = apollo;
                asio.typeName = "ASIO";
                expect (! session.isPresent (asio));

                ReconnectWatch watch;
                watch.reset (session.isPresent (apollo));
                expect (! watch.update (true, true, true), "running and present: nothing to do");

                // Unplugged while open: the app closes it (here: the status says not open).
                unplug();
                fake.close();
                expect (! session.isPresent (apollo));
                expect (! watch.update (session.isPresent (apollo), false, true));
                expect (! watch.update (false, false, true), "still gone");

                // Plugged back in while a take records: remembered, not reopened yet.
                plugIn();
                expect (session.isPresent (apollo));
                expect (! watch.update (true, false, false));
                expect (watch.isPending());
                expect (! watch.update (true, false, false));

                // Free again: reopen once.
                expect (watch.update (true, false, true));
                expect (! watch.isPending());
                expect (session.open (apollo, true).ok);
                expect (! watch.update (true, true, true), "reopened: nothing more to do");

                // Missing at launch (another device opened as a fallback), then plugged in.
                unplug();
                ReconnectWatch launch;
                launch.reset (session.isPresent (apollo));
                expect (! launch.update (false, false, true));
                plugIn();
                expect (launch.update (true, false, true), "the saved device appeared after launch");

                // Back, but the user already picked it again by hand: nothing to do.
                unplug();
                expect (! launch.update (false, false, true));
                plugIn();
                expect (! launch.update (true, true, true));
                expect (! launch.isPending());
            }
        }
    };

    static DeviceSessionTests deviceSessionTests;
}
