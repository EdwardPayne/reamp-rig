#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  44 px bar across the top of the window: app name, device summary,
        sync status chip and the batch transport (Start / Pause / Stop).
        The device summary and sync chip are live since phase 3; the transport is wired up
        in phase 4.
    */
    class TopBar final : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        TopBar();

        /** e.g. "Apollo Twin · 48 kHz · 256" or "No device". */
        void setDeviceSummary (const juce::String& summary, const juce::String& tooltip = {});
        const juce::String& getDeviceSummary() const noexcept   { return deviceSummary; }

        void setSyncStatus (const juce::String& text, juce::Colour colour, const juce::String& tooltip);

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
        juce::String deviceTooltip;

        juce::String getTooltip() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
    };
}
