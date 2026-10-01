#include "App/Settings.h"
#include "TestHelpers.h"

namespace rf::test
{
    using rf::app::Settings;

    /*  Settings round-trip (PROMPT.md section 7) for every key persisted so far: include
        subfolders (phase 2), the device configuration and the output gain (phase 3), the
        output-file and batch options (phase 4).
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

            beginTest ("phase 4 defaults");
            {
                const auto fresh = dir.get().getChildFile ("fresh.settings");
                Settings settings (fresh);

                expectEquals (settings.getTailMs(), 0);
                expectEquals (settings.getPrefix(), juce::String());
                expectEquals (settings.getSuffix(), juce::String ("_reamp"));
                expect (settings.getDestinationMode() == model::DestinationMode::besideSource);
                expectEquals (settings.getSubfolderName(), juce::String ("Reamped"));
                expect (settings.getOutputFolder() == juce::File());
                expect (settings.getMirrorStructure());
                expect (! settings.getChannelTag());
                expectEquals (settings.getBitDepth(), 24);
                expect (settings.getCollisionPolicy() == model::CollisionPolicy::autoNumber);
                expect (settings.getNamingOptions() == model::NamingOptions {});
            }

            beginTest ("phase 4 keys survive a save and reload");
            {
                const auto outFolder = dir.get().getChildFile (juce::String (juce::CharPointer_UTF8 ("ReÃ¤mps/Out")));

                {
                    Settings settings (file);
                    settings.setTailMs (1500);
                    settings.setPrefix ("amp_");
                    settings.setSuffix (juce::String (juce::CharPointer_UTF8 ("_mÃ¤rshall")));
                    settings.setDestinationMode (model::DestinationMode::singleFolder);
                    settings.setSubfolderName ("Amped");
                    settings.setOutputFolder (outFolder);
                    settings.setMirrorStructure (false);
                    settings.setChannelTag (true);
                    settings.setBitDepth (32);
                    settings.setCollisionPolicy (model::CollisionPolicy::skip);
                }

                Settings reloaded (file);
                expectEquals (reloaded.getTailMs(), 1500);
                expectEquals (reloaded.getPrefix(), juce::String ("amp_"));
                expectEquals (reloaded.getSuffix(), juce::String (juce::CharPointer_UTF8 ("_mÃ¤rshall")));
                expect (reloaded.getDestinationMode() == model::DestinationMode::singleFolder);
                expectEquals (reloaded.getSubfolderName(), juce::String ("Amped"));
                expectEquals (reloaded.getOutputFolder().getFullPathName(), outFolder.getFullPathName());
                expect (! reloaded.getMirrorStructure());
                expect (reloaded.getChannelTag());
                expectEquals (reloaded.getBitDepth(), 32);
                expect (reloaded.getCollisionPolicy() == model::CollisionPolicy::skip);

                const auto o = reloaded.getNamingOptions();
                expect (o.mode == model::DestinationMode::singleFolder && o.outputFolder == outFolder && ! o.mirrorStructure
                        && o.prefix == "amp_" && o.subfolderName == "Amped" && o.channelTag
                        && o.collision == model::CollisionPolicy::skip);

                for (const auto policy : { model::CollisionPolicy::overwrite, model::CollisionPolicy::autoNumber })
                {
                    {
                        Settings settings (file);
                        settings.setCollisionPolicy (policy);
                        settings.setBitDepth (16);
                        settings.setDestinationMode (model::DestinationMode::besideSource);
                    }

                    Settings again (file);
                    expect (again.getCollisionPolicy() == policy);
                    expectEquals (again.getBitDepth(), 16);
                    expect (again.getDestinationMode() == model::DestinationMode::besideSource);
                }
            }

            beginTest ("phase 4 garbage and limits");
            {
                {
                    Settings settings (file);
                    settings.setTailMs (999999);
                }

                expectEquals (Settings (file).getTailMs(), Settings::maxTailMs);

                {
                    Settings settings (file);
                    auto& props = settings.getPropertiesFile();
                    props.setValue ("tailMs", "-20");
                    props.setValue ("bitDepth", "20");
                    props.setValue ("collisionPolicy", "explode");
                    props.setValue ("destinationMode", "cloud");
                    props.setValue ("subfolderName", "   ");
                    props.setValue ("outputFolder", "relative/path");
                }

                Settings reloaded (file);
                expectEquals (reloaded.getTailMs(), 0);
                expectEquals (reloaded.getBitDepth(), 24);
                expect (reloaded.getCollisionPolicy() == model::CollisionPolicy::autoNumber);
                expect (reloaded.getDestinationMode() == model::DestinationMode::besideSource);
                expectEquals (reloaded.getSubfolderName(), juce::String ("Reamped"));
                expect (reloaded.getOutputFolder() == juce::File());
            }
        }
    };

    static SettingsTests settingsTests;
}
