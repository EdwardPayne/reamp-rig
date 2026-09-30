#include <juce_gui_basics/juce_gui_basics.h>

#include "App/MacAppearance.h"
#include "App/MainWindow.h"
#include "App/Snapshot.h"
#include "UI/LookAndFeel.h"

class ReampForgeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        rf::app::useDarkAppearance();

        lookAndFeel = std::make_unique<rf::ui::ForgeLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<rf::app::MainWindow> (getApplicationName());

        // Development aid: `--snapshot=<file.png>` renders the window to a PNG and quits.
        const juce::ArgumentList args (getApplicationName(), commandLine);

        if (args.containsOption ("--snapshot"))
            scheduleSnapshot (juce::File::getCurrentWorkingDirectory()
                                  .getChildFile (args.getValueForOption ("--snapshot")));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    void scheduleSnapshot (const juce::File& pngFile)
    {
        juce::Timer::callAfterDelay (1500, [this, pngFile]
        {
            const auto ok = mainWindow != nullptr && rf::app::writeSnapshot (*mainWindow, pngFile);
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
        });
    }

    std::unique_ptr<rf::ui::ForgeLookAndFeel> lookAndFeel;
    std::unique_ptr<rf::app::MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ReampForgeApplication)
