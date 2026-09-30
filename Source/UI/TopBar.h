#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  44 px bar across the top of the window: app name, device summary,
        sync status chip and the batch transport (Start / Pause / Stop).
        Phase 1: placeholder content only, nothing is wired up yet.
    */
    class TopBar final : public juce::Component
    {
    public:
        TopBar();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class SyncChip final : public juce::Component,
                               public juce::SettableTooltipClient
        {
        public:
            SyncChip();

            void setStatus (const juce::String& text, juce::Colour colour);
            int getIdealWidth() const;
            void paint (juce::Graphics&) override;

        private:
            juce::String statusText;
            juce::Colour statusColour;
        };

        juce::String deviceSummary;
        SyncChip syncChip;
        juce::TextButton startButton { "Start" }, pauseButton { "Pause" }, stopButton { "Stop" };

        juce::Rectangle<int> brandArea, deviceArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
    };
}
