#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  44 px bar across the top of the window: app name, device summary, sync status chip
        and the batch transport (Start / Pause-Resume / Skip / Stop). The app sets the
        transport state and receives the button presses through the callbacks.
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

        enum class Transport { idle, running, paused };

        /** Enables the buttons for the batch state; Pause reads "Resume" while paused. */
        void setTransport (Transport);
        Transport getTransport() const noexcept   { return transport; }

        std::function<void()> onStart, onPauseResume, onSkip, onStop;

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
        juce::TextButton startButton { "Start" }, pauseButton { "Pause" }, skipButton { "Skip" }, stopButton { "Stop" };
        Transport transport = Transport::idle;

        juce::Rectangle<int> brandArea, deviceArea;
        juce::String deviceTooltip;

        juce::String getTooltip() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
    };
}
