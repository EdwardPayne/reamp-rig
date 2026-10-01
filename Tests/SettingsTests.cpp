#include "App/Settings.h"
#include "TestHelpers.h"

namespace rf::test
{
    using rf::app::Settings;

    /*  Settings round-trip (PROMPT.md section 7) for every key persisted so far: include
        subfolders (phase 2), the device configuration and the output gain (phase 3).
        Uses a settings file in a temporary folder, never the user's own file.
    */
    class SettingsTests final : public juce::UnitTest
    {
    public:
        SettingsTests() : juce::UnitTest ("Settings", "Settings") {}

        void runTest() override
        {
            TempDirectory dir;
            const auto file = dir.get().getChildFile ("Reamp Forge.settings");

            beginTest ("defaults on a fresh file");
            {
                Settings settings (file);
                const auto c = settings.getDeviceConfig();

                expect (settings.getIncludeSubfolders());
                expect (c.typeName.isEmpty() && c.inputDevice.isEmpty() && c.outputDevice.isEmpty());
                expectEquals (c.sampleRate, 0.0);
                expectEquals (c.bufferSize, 0);
                expectEquals (c.inputChannel, -1);
                expectEquals (c.outputChannel, -1);
                expect (c.inputChannelName.isEmpty() && c.outputChannelName.isEmpty());
                expectEquals (settings.getOutputGainDb(), 0.0f);
            }

            beginTest ("every key survives a save and reload");
            {
                engine::DeviceConfig c;
                c.typeName = "CoreAudio";
                c.inputDevice = "Apollo Twin";
                c.outputDevice = "Apollo Twin";
                c.sampleRate = 96000.0;
                c.bufferSize = 480;
                c.inputChannel = 2;
                c.inputChannelName = "Hi-Z 1";
                c.outputChannel = 3;
                c.outputChannelName = "Line 4";

                {
                    Settings settings (file);
                    settings.setIncludeSubfolders (false);
                    settings.setDeviceConfig (c);
                    settings.setOutputGainDb (-12.5f);
                }   // destructor saves

                expect (file.existsAsFile());

                Settings reloaded (file);
                expect (! reloaded.getIncludeSubfolders());
                expect (reloaded.getDeviceConfig() == c);
                expectWithinAbsoluteError (reloaded.getOutputGainDb(), -12.5f, 1.0e-6f);
            }

            beginTest ("split devices and non-ASCII names round-trip");
            {
                engine::DeviceConfig c;
                c.typeName = "CoreAudio";
                c.inputDevice = "MacBook Pro Microphone";
                c.outputDevice = juce::String (juce::CharPointer_UTF8 ("Z\xc3\xbcrich Speakers \xc2\xb7 2"));
                c.sampleRate = 44100.0;
                c.bufferSize = 512;
                c.inputChannel = 0;
                c.inputChannelName = "Input 1";
                c.outputChannel = 1;
                c.outputChannelName = "Output 2";

                {
                    Settings settings (file);
                    settings.setDeviceConfig (c);
                    settings.save();
                }

                Settings reloaded (file);
                expect (reloaded.getDeviceConfig() == c);
                expect (reloaded.getDeviceConfig().isSplit());
            }

            beginTest ("output gain is clamped to -60..+12 dB");
            {
                {
                    Settings settings (file);
                    settings.setOutputGainDb (40.0f);
                }

                expectEquals (Settings (file).getOutputGainDb(), 12.0f);

                {
                    Settings settings (file);
                    settings.setOutputGainDb (-200.0f);
                }

                expectEquals (Settings (file).getOutputGainDb(), -60.0f);
            }

            beginTest ("hand-edited garbage falls back to safe values");
            {
                {
                    Settings settings (file);
                    auto& props = settings.getPropertiesFile();
                    props.setValue ("outputGainDb", "loud");
                    props.setValue ("bufferSize", "-64");
                    props.setValue ("sampleRate", "-1");
                    props.setValue ("inputChannel", "-7");
                    props.setValue ("outputChannel", "banana");
                }

                Settings reloaded (file);
                const auto c = reloaded.getDeviceConfig();
                expectEquals (reloaded.getOutputGainDb(), 0.0f);
                expectEquals (c.bufferSize, 0);
                expectEquals (c.sampleRate, 0.0);
                expectEquals (c.inputChannel, -1);
                expectEquals (c.outputChannel, 0);   // "banana" parses as 0; resolved against the device later
            }
        }
    };

    static SettingsTests settingsTests;
}
