#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rf::ui
{
    enum class FontWeight { regular, medium, bold };

    /*  The embedded JetBrains Mono and Inter typefaces.

        The typefaces are loaded from binary data the first time an instance is created and
        shared between all instances (juce::SharedResourcePointer), so holding one for the
        lifetime of the app (the LookAndFeel does) keeps them alive; the static helpers below
        can then be called cheaply from anywhere on the message thread.

        Use mono for headings, section labels, buttons, values and table cells;
        use sans (Inter) for prose and help text.
    */
    class Fonts
    {
    public:
        Fonts();

        juce::Typeface::Ptr getMonoTypeface (FontWeight) const;
        juce::Typeface::Ptr getSansTypeface (FontWeight) const;

        static juce::Font mono (float pointSize, FontWeight = FontWeight::regular, float tracking = 0.0f);
        static juce::Font sans (float pointSize, FontWeight = FontWeight::regular, float tracking = 0.0f);

        /** Small uppercase letter-spaced mono section label font (e.g. "DESTINATION"). */
        static juce::Font sectionLabel();

    private:
        struct Typefaces;
        juce::SharedResourcePointer<Typefaces> typefaces;
    };
}
