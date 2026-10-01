#include <juce_gui_basics/juce_gui_basics.h>

#include "App/CommandLine.h"
#include "App/MacAppearance.h"
#include "App/MainComponent.h"
#include "App/MainWindow.h"
#include "App/Settings.h"
#include "App/Snapshot.h"
#include "UI/LookAndFeel.h"

class ReampForgeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        rf::app::useDarkAppearance();

        lookAndFeel = std::make_unique<rf::ui::ForgeLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        // --open=<path> adds files/folders; --snapshot, the device flags and friends are
        // development aids. See App/CommandLine.h.
        const auto options = rf::app::LaunchOptions::parse (getCommandLineParameterArray(),
                                                            juce::File::getCurrentWorkingDirectory());

        settings = std::make_unique<rf::app::Settings>();
        mainWindow = std::make_unique<rf::app::MainWindow> (getApplicationName(), *settings, options.virtualDevice);

        std::function<void (bool)> onAuditionCheckDone;

        if (options.auditionCheckSeconds.has_value() && ! options.snapshotFile.has_value())
            onAuditionCheckDone = [this] (bool ok)
            {
                setApplicationReturnValue (ok ? 0 : 1);
                quit();
            };

        mainWindow->getMainComponent().applyLaunchOptions (options, onAuditionCheckDone);

        if (options.snapshotFile.has_value())
            scheduleSnapshot (*options.snapshotFile);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        settings = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    /** Waits until the window has settled (at least 1.5 s, and until scans and the waveform
        have finished, at most 20 s), then renders it to a PNG and quits. */
    void scheduleSnapshot (const juce::File& pngFile)
    {
        snapshotStarted = juce::Time::getMillisecondCounter();
        juce::Timer::callAfterDelay (200, [this, pngFile] { pollSnapshot (pngFile); });
    }

    void pollSnapshot (const juce::File& pngFile)
    {
        if (mainWindow == nullptr)
            return;

        const auto elapsed = juce::Time::getMillisecondCounter() - snapshotStarted;
        const auto busy = mainWindow->getMainComponent().isBusy();

        if (elapsed < 1500 || (busy && elapsed < 20000))
        {
            juce::Timer::callAfterDelay (100, [this, pngFile] { pollSnapshot (pngFile); });
            return;
        }

        const auto ok = rf::app::writeSnapshot (*mainWindow, pngFile);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    std::unique_ptr<rf::ui::ForgeLookAndFeel> lookAndFeel;
    std::unique_ptr<rf::app::Settings> settings;
    std::unique_ptr<rf::app::MainWindow> mainWindow;
    juce::uint32 snapshotStarted = 0;
};

START_JUCE_APPLICATION (ReampForgeApplication)
