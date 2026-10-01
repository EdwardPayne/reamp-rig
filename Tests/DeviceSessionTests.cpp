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
        }
    };

    static DeviceSessionTests deviceSessionTests;
}
