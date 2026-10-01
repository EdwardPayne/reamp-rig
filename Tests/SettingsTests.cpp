#include "App/Settings.h"
#include "TestHelpers.h"

namespace rf::test
{
    using rf::app::Settings;
    using rf::app::clampWindowBounds;

    /*  Settings round-trip (PROMPT.md section 7) for every key persisted so far: include
        subfolders (phase 2), the device configuration and the output gain (phase 3), the
        output-file and batch options (phase 4), the sync level and the keyed sync store
        (phase 5).
        Uses a settings file in a temporary folder, never the user's own file.
    */
    class SettingsTests final : public juce::UnitTest
    {
    public:
        SettingsTests() : juce::UnitTest ("Settings", "Settings") {}

        void runTest() override
        {
            TempDirectory dir;
            const auto file = dir.get().getChildFile ("Reamp Rig.settings");

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

            testSyncStore (dir);
        }

        //==============================================================================
        static engine::SyncKey syncKey (const juce::String& device, double rate, int buffer,
                                        const juce::String& input = {})
        {
            return { "CoreAudio", input.isNotEmpty() ? input : device, device, rate, buffer };
        }

        static engine::SyncMeasurement measurement (int samples, double rate, engine::SyncConfidence c, float peakDb = -12.3f)
        {
            engine::SyncMeasurement m;
            m.samples = samples;
            m.ms = samples * 1000.0 / rate;
            m.returnedPeakDb = peakDb;
            m.peakToSidelobeDb = 31.25;
            m.repeatsUsed = 5;
            m.repeatsTotal = 5;
            m.confidence = c;
            m.date = juce::Time (2026, 9, 1, 14, 2, 7);     // 1 October 2026 (months are 0-based)
            return m;
        }

        void testSyncStore (const TempDirectory& dir)
        {
            using engine::SyncConfidence;
            const auto file = dir.get().getChildFile ("sync.settings");

            beginTest ("sync: level default -12 dBFS, clamped to -60..0");
            {
                {
                    Settings settings (file);
                    expectEquals (settings.getSyncLevelDb(), -12.0f);
                    settings.setSyncLevelDb (-24.5f);
                }

                expectEquals (Settings (file).getSyncLevelDb(), -24.5f);

                {
                    Settings settings (file);
                    settings.setSyncLevelDb (6.0f);
                }

                expectEquals (Settings (file).getSyncLevelDb(), 0.0f);

                {
                    Settings settings (file);
                    settings.getPropertiesFile().setValue ("syncLevelDb", "-200");
                }

                expectEquals (Settings (file).getSyncLevelDb(), -60.0f);

                {
                    Settings settings (file);
                    settings.getPropertiesFile().setValue ("syncLevelDb", "nan");
                }

                expectEquals (Settings (file).getSyncLevelDb(), -12.0f);
            }

            beginTest ("sync store: several keys round-trip; rate, buffer and devices each make a key");
            {
                const auto apollo48 = syncKey ("Apollo Twin", 48000.0, 256);
                const auto apollo96 = syncKey ("Apollo Twin", 96000.0, 256);
                const auto apollo48b = syncKey ("Apollo Twin", 48000.0, 512);
                const auto split = syncKey ("MacBook Pro Speakers", 48000.0, 512, "MacBook Pro Microphone");
                const auto unicode = syncKey (juce::String (juce::CharPointer_UTF8 ("Z\xc3\xbcrich \xc2\xb7 Box")), 44100.0, 64);

                {
                    Settings settings (file);
                    expect (settings.getSyncMeasurements().empty());
                    expect (! settings.getSyncMeasurement (apollo48).has_value());

                    settings.setSyncMeasurement (apollo48, measurement (312, 48000.0, SyncConfidence::high));
                    settings.setSyncMeasurement (apollo96, measurement (598, 96000.0, SyncConfidence::medium, -9.5f));
                    settings.setSyncMeasurement (apollo48b, measurement (824, 48000.0, SyncConfidence::low));
                    settings.setSyncMeasurement (split, measurement (5012, 48000.0, SyncConfidence::low, -41.0f));
                    settings.setSyncMeasurement (unicode, measurement (101, 44100.0, SyncConfidence::high));
                }

                Settings reloaded (file);
                expectEquals ((int) reloaded.getSyncMeasurements().size(), 5);
                expect (reloaded.getSyncMeasurement (apollo48) == measurement (312, 48000.0, SyncConfidence::high));
                expect (reloaded.getSyncMeasurement (apollo96) == measurement (598, 96000.0, SyncConfidence::medium, -9.5f));
                expect (reloaded.getSyncMeasurement (apollo48b) == measurement (824, 48000.0, SyncConfidence::low));
                expect (reloaded.getSyncMeasurement (split) == measurement (5012, 48000.0, SyncConfidence::low, -41.0f));
                expect (reloaded.getSyncMeasurement (unicode) == measurement (101, 44100.0, SyncConfidence::high));

                const auto m = *reloaded.getSyncMeasurement (apollo48);
                expectEquals (m.samples, 312);
                expectWithinAbsoluteError (m.ms, 6.5, 1.0e-9);
                expectWithinAbsoluteError (m.returnedPeakDb, -12.3f, 0.005f);
                expectWithinAbsoluteError (m.peakToSidelobeDb, 31.25, 0.005);
                expectEquals (m.date.getYear(), 2026);
                expectEquals (m.date.getMonth(), 9);
                expectEquals (m.date.getDayOfMonth(), 1);
                expectEquals (m.date.getHours(), 14);

                // Not stored: another rate, another buffer, the input device swapped, another driver.
                expect (! reloaded.getSyncMeasurement (syncKey ("Apollo Twin", 44100.0, 256)).has_value());
                expect (! reloaded.getSyncMeasurement (syncKey ("Apollo Twin", 48000.0, 128)).has_value());
                expect (! reloaded.getSyncMeasurement (syncKey ("MacBook Pro Microphone", 48000.0, 512, "MacBook Pro Speakers")).has_value());
                auto asio = apollo48;
                asio.typeName = "ASIO";
                expect (! reloaded.getSyncMeasurement (asio).has_value());
            }

            beginTest ("sync store: a new measurement overwrites the old one for its key only");
            {
                const auto apollo48 = syncKey ("Apollo Twin", 48000.0, 256);

                {
                    Settings settings (file);
                    settings.setSyncMeasurement (apollo48, measurement (313, 48000.0, SyncConfidence::medium, -11.0f));
                }

                Settings reloaded (file);
                expectEquals ((int) reloaded.getSyncMeasurements().size(), 5);
                expectEquals (reloaded.getSyncMeasurement (apollo48)->samples, 313);
                expect (reloaded.getSyncMeasurement (apollo48)->confidence == SyncConfidence::medium);
                expectEquals (reloaded.getSyncMeasurement (syncKey ("Apollo Twin", 96000.0, 256))->samples, 598);
            }

            beginTest ("sync store: invalid keys and empty measurements are never stored");
            {
                Settings settings (file);
                settings.setSyncMeasurement ({}, measurement (100, 48000.0, SyncConfidence::high));
                settings.setSyncMeasurement (syncKey ("Apollo Twin", 0.0, 256), measurement (100, 48000.0, SyncConfidence::high));
                settings.setSyncMeasurement (syncKey ("Apollo Twin", 48000.0, 0), measurement (100, 48000.0, SyncConfidence::high));
                settings.setSyncMeasurement (syncKey ("Apollo Twin", 44100.0, 256), measurement (0, 44100.0, SyncConfidence::high));
                expectEquals ((int) settings.getSyncMeasurements().size(), 5);
            }

            beginTest ("sync store: hand-edited garbage is ignored entry by entry");
            {
                const auto garbageFile = dir.get().getChildFile ("garbage.settings");

                {
                    Settings settings (garbageFile);
                    settings.getPropertiesFile().setValue ("syncMeasurements", "this is not xml <<<");
                }

                expect (Settings (garbageFile).getSyncMeasurements().empty());

                {
                    Settings settings (garbageFile);
                    settings.getPropertiesFile().setValue ("syncMeasurements", "<OTHER><MEASUREMENT type=\"CoreAudio\"/></OTHER>");
                }

                expect (Settings (garbageFile).getSyncMeasurements().empty());

                const juce::String good = "type=\"CoreAudio\" input=\"Box\" output=\"Box\" rate=\"48000\" buffer=\"256\" ";

                juce::XmlElement root ("SYNC");
                auto add = [&] (const juce::String& attributes)
                {
                    root.addChildElement (juce::parseXML ("<MEASUREMENT " + attributes + "/>").release());
                };

                add (good + "samples=\"300\" confidence=\"high\" date=\"2026-10-01T14:02:07.000+02:00\" peakDb=\"-12\"");   // valid
                add (good + "samples=\"-5\" confidence=\"high\"");                    // negative round trip
                add (good + "samples=\"banana\" confidence=\"high\"");                // not a number
                add (good + "samples=\"999999999\" confidence=\"high\"");             // longer than 10 s
                add (good + "samples=\"300\" confidence=\"great\"");                  // unknown confidence
                add (good + "samples=\"300\"");                                       // no confidence
                add ("type=\"CoreAudio\" input=\"Box\" output=\"Box\" rate=\"fast\" buffer=\"256\" samples=\"1\" confidence=\"low\"");
                add ("type=\"CoreAudio\" input=\"Box\" output=\"Box\" rate=\"44100\" buffer=\"-1\" samples=\"1\" confidence=\"low\"");
                add ("type=\"\" input=\"Box\" output=\"Box\" rate=\"44100\" buffer=\"64\" samples=\"1\" confidence=\"low\"");
                add ("type=\"CoreAudio\" input=\"Box\" output=\"Box\" rate=\"96000\" buffer=\"64\" samples=\"77\" "
                     "confidence=\"low\" ms=\"999\" peakDb=\"loud\" psrDb=\"inf\" used=\"9\" total=\"5\" date=\"yesterday\"");  // valid, junk extras

                {
                    Settings settings (garbageFile);
                    settings.getPropertiesFile().setValue ("syncMeasurements", &root);
                }

                Settings reloaded (garbageFile);
                const auto entries = reloaded.getSyncMeasurements();
                expectEquals ((int) entries.size(), 2);

                const auto first = reloaded.getSyncMeasurement ({ "CoreAudio", "Box", "Box", 48000.0, 256 });
                expect (first.has_value() && first->samples == 300 && first->confidence == SyncConfidence::high);
                expectEquals (first->date.getYear(), 2026);
                expectWithinAbsoluteError (first->ms, 6.25, 1.0e-9);

                const auto second = reloaded.getSyncMeasurement ({ "CoreAudio", "Box", "Box", 96000.0, 64 });
                expect (second.has_value() && second->samples == 77);
                expectWithinAbsoluteError (second->ms, 77.0 / 96.0, 1.0e-9);     // derived, not the stored 999
                expect (second->repeatsUsed <= second->repeatsTotal);
                expectEquals (second->date.toMilliseconds(), (juce::int64) 0);  // unknown date
                expect (std::isfinite (second->peakToSidelobeDb));

                // A valid write after garbage keeps the valid entries and drops the rest.
                reloaded.setSyncMeasurement ({ "CoreAudio", "Box", "Box", 44100.0, 64 }, measurement (50, 44100.0, SyncConfidence::medium));
                expectEquals ((int) reloaded.getSyncMeasurements().size(), 3);
            }

            beginTest ("sync store: Forget removes one key only and survives a reload");
            {
                const auto apollo48 = syncKey ("Apollo Twin", 48000.0, 256);
                const auto apollo96 = syncKey ("Apollo Twin", 96000.0, 256);

                {
                    Settings settings (file);
                    expectEquals ((int) settings.getSyncMeasurements().size(), 5);
                    expect (settings.removeSyncMeasurement (apollo48));
                    expect (! settings.removeSyncMeasurement (apollo48));                              // already gone
                    expect (! settings.removeSyncMeasurement (syncKey ("Apollo Twin", 44100.0, 256))); // never stored
                    expect (! settings.getSyncMeasurement (apollo48).has_value());
                }

                Settings reloaded (file);
                expectEquals ((int) reloaded.getSyncMeasurements().size(), 4);
                expect (! reloaded.getSyncMeasurement (apollo48).has_value());
                expectEquals (reloaded.getSyncMeasurement (apollo96)->samples, 598);

                // Measuring again after a Forget stores it as new.
                reloaded.setSyncMeasurement (apollo48, measurement (314, 48000.0, SyncConfidence::high));
                expectEquals (reloaded.getSyncMeasurement (apollo48)->samples, 314);
                expectEquals ((int) reloaded.getSyncMeasurements().size(), 5);
            }

            beginTest ("pause between files: default 2 s, round trip, clamped to 0..60 s in tenths");
            {
                const auto pauseFile = dir.get().getChildFile ("pause.settings");

                expectEquals (Settings (pauseFile).getPauseBetweenFilesSeconds(), 2.0);

                for (const auto& [in, out] : { std::pair<double, double> { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.5, 2.5 }, { 0.04, 0.0 },
                                               { 0.36, 0.4 }, { 60.0, 60.0 }, { 61.0, 60.0 }, { -3.0, 0.0 },
                                               { std::numeric_limits<double>::quiet_NaN(), 2.0 },
                                               { std::numeric_limits<double>::infinity(), 2.0 } })
                {
                    {
                        Settings settings (pauseFile);
                        settings.setPauseBetweenFilesSeconds (in);
                    }

                    expectEquals (Settings (pauseFile).getPauseBetweenFilesSeconds(), out, "in " + juce::String (in));
                }

                for (const auto& [text, out] : { std::pair<const char*, double> { "garbage", 2.0 }, { "", 2.0 }, { "7", 7.0 },
                                                 { "1e9", 60.0 }, { "-1", 0.0 }, { "0.25", 0.3 } })
                {
                    {
                        Settings settings (pauseFile);
                        settings.getPropertiesFile().setValue ("pauseBetweenFilesSeconds", text);
                    }

                    expectEquals (Settings (pauseFile).getPauseBetweenFilesSeconds(), out, juce::String ("text ") + text);
                }
            }

            beginTest ("window bounds: round trip, nothing saved, garbage ignored");
            {
                const auto windowFile = dir.get().getChildFile ("window.settings");

                expect (! Settings (windowFile).getWindowBounds().has_value());

                for (const auto r : { juce::Rectangle<int> (120, 80, 1300, 820), juce::Rectangle<int> (-1800, -200, 1100, 700) })
                {
                    {
                        Settings settings (windowFile);
                        settings.setWindowBounds (r);
                        settings.setWindowBounds ({});        // empty bounds are ignored
                    }

                    expect (Settings (windowFile).getWindowBounds() == std::optional<juce::Rectangle<int>> (r), r.toString());
                }

                for (const auto* text : { "1 2 3", "a b c d", "10 10 0 700", "10 10 1100 -5", "1 2 3 4 5", "", "999999999 0 1100 700" })
                {
                    {
                        Settings settings (windowFile);
                        settings.getPropertiesFile().setValue ("windowBounds", text);
                    }

                    expect (! Settings (windowFile).getWindowBounds().has_value(), juce::String ("text '") + text + "'");
                }
            }

            beginTest ("window bounds are clamped to a visible display and the 1100 x 700 minimum");
            {
                using R = juce::Rectangle<int>;
                const juce::Point<int> minSize (1100, 700);
                const R main (0, 25, 1512, 957);              // MacBook Pro user area (below the menu bar)
                const R left (-1920, 0, 1920, 1055);          // external display left of it
                const juce::Array<R> both { main, left };

                // Fits: unchanged.
                expect (clampWindowBounds (R (100, 80, 1280, 800), both, minSize) == std::optional<R> (R (100, 80, 1280, 800)));

                // On the second display: stays there.
                expect (clampWindowBounds (R (-1800, 50, 1300, 820), both, minSize) == std::optional<R> (R (-1800, 50, 1300, 820)));

                // Too small: grows to the minimum.
                expect (clampWindowBounds (R (100, 80, 400, 300), both, minSize) == std::optional<R> (R (100, 80, 1100, 700)));

                // Larger than the display: shrinks to it and moves inside.
                expect (clampWindowBounds (R (-50, 0, 3000, 2000), juce::Array<R> { main }, minSize) == std::optional<R> (main));

                // Hanging off the right and bottom edges: moved back inside, size kept.
                expect (clampWindowBounds (R (1000, 600, 1200, 750), juce::Array<R> { main }, minSize)
                        == std::optional<R> (R (312, 232, 1200, 750)));

                // Above the menu bar: the title bar comes back below it.
                expectEquals (clampWindowBounds (R (100, -300, 1200, 750), juce::Array<R> { main }, minSize)->getY(), 25);

                // The display it was on is gone (monitor unplugged): centred on the main display.
                const auto moved = clampWindowBounds (R (-1800, 50, 1300, 820), juce::Array<R> { main }, minSize);
                expect (moved.has_value() && main.contains (*moved), moved->toString());
                expect (moved->getCentre().getDistanceFrom (main.getCentre()) <= 1, moved->toString());
                expectEquals (moved->getWidth(), 1300);

                // A display smaller than the minimum: the minimum wins, top-left on the display.
                expect (clampWindowBounds (R (0, 0, 1280, 800), juce::Array<R> { R (0, 0, 1024, 600) }, minSize)
                        == std::optional<R> (R (0, 0, 1100, 700)));

                // No display at all.
                expect (! clampWindowBounds (R (0, 0, 1280, 800), {}, minSize).has_value());
            }
        }
    };

    static SettingsTests settingsTests;
}
