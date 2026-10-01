#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CommandLine.h"
#include "Settings.h"

namespace rf::app
{
    class MainComponent;

    /*  The single, resizable application window (native title bar).

        Size and position are remembered (PROMPT.md 3.7, section 5): saved on every move and
        resize (outer frame, so the title bar is included) and restored on launch, clamped to
        a display that is present and to the 1100 x 700 minimum (clampWindowBounds). Full
        screen and minimised states are not saved; the window comes back as a normal window.
    */
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, Settings&, const LaunchOptions&);
        ~MainWindow() override;

        MainComponent& getMainComponent();

        void closeButtonPressed() override;
        void moved() override;
        void resized() override;

        /** Writes the current bounds to the settings now (also done on every move/resize). */
        void saveBounds();

    private:
        void restoreBounds();
        juce::BorderSize<int> getFrame() const;

        Settings& settings;
        bool boundsRestored = false;    // no saving until the saved bounds were applied

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}
