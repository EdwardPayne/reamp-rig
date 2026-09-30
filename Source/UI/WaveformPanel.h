#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  Bottom panel: source waveform above, recorded result below, on a shared time axis.
        Phase 1: placeholder that draws the header, time ruler and the two empty lanes.
    */
    class WaveformPanel final : public juce::Component
    {
    public:
        WaveformPanel() = default;

        void paint (juce::Graphics&) override;

    private:
        void paintRuler (juce::Graphics&, juce::Rectangle<int> area) const;
        void paintLane (juce::Graphics&, juce::Rectangle<int> area, const juce::String& name,
                        const juce::String& emptyText) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPanel)
    };
}
