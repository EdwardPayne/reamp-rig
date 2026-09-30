#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /** One-line status strip at the bottom of the window (batch progress, ETA, messages). */
    class StatusBar final : public juce::Component
    {
    public:
        StatusBar();

        void setStatus (const juce::String& left, const juce::String& right);
        void paint (juce::Graphics&) override;

    private:
        juce::String leftText, rightText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
    };
}
