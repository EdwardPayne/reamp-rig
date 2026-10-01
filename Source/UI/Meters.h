#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  Horizontal peak meter with a latching clip indicator (PROMPT.md section 3.2.3).

        [OUT |██████████░░░░░░░░|  -18.2  [CLIP]]

        Scale -60..0 dBFS. The bar falls back at 24 dB/s; the peak hold (thin line and the
        readout) holds for 1.5 s. The clip box lights in `warn` and stays lit until the meter
        is clicked. Fed at the UI rate from the engine snapshot (message thread).
    */
    class LevelMeter final : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        explicit LevelMeter (juce::String label);

        /** New peak (linear) since the last call, and whether full scale was reached. */
        void push (float peak, bool clipped);

        /** Drops the bar to silence (device closed). Keeps a latched clip. */
        void reset();

        bool isClipLatched() const noexcept   { return clipLatched; }
        void clearClip();

        float getDisplayedDb() const noexcept { return displayDb; }

        static constexpr float floorDb = -60.0f;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        float dbToProportion (float db) const noexcept;

        juce::String label;
        float displayDb = floorDb;      // falling bar
        float holdDb = floorDb;         // peak hold
        double holdUntilMs = 0.0;
        double lastUpdateMs = 0.0;
        bool clipLatched = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
    };
}
