#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CommandLine.h"
#include "Settings.h"

namespace rf::app
{
    class MainComponent;

    /** The single, resizable application window (native title bar). */
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, Settings&, const LaunchOptions&);

        MainComponent& getMainComponent();

        void closeButtonPressed() override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}
