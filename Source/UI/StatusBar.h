#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  One-line status strip at the bottom of the window. The left side carries the latest
        message (batch progress/ETA from phase 4, scan results and warnings now); the right
        side carries the queue summary ("N files queued"). Warnings and errors get a badge so
        they are not signalled by colour alone; `detail` becomes the tooltip.
    */
    class StatusBar final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        enum class Tone { normal, warning, error };

        StatusBar();

        void setMessage (const juce::String& text, Tone = Tone::normal, const juce::String& detail = {});
        void setQueueText (const juce::String&);

        const juce::String& getMessage() const noexcept   { return message; }
        Tone getTone() const noexcept                     { return tone; }

        void paint (juce::Graphics&) override;

    private:
        juce::String message, queueText;
        Tone tone = Tone::normal;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
    };
}
