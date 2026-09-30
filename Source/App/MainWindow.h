#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::app
{
    /** The single, resizable application window (native title bar). */
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name);

        void closeButtonPressed() override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}
