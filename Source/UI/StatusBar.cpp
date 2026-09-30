#include "StatusBar.h"
#include "Fonts.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace rf::ui
{
    namespace colour = theme::colour;

    namespace
    {
        constexpr int badgeSize = 12;
    }

    StatusBar::StatusBar()
    {
        setMessage ("Ready");
        setQueueText ("0 files queued");
    }

    void StatusBar::setMessage (const juce::String& text, Tone newTone, const juce::String& detail)
    {
        message = text;
        tone = newTone;
        setTooltip (detail);
        repaint();
    }

    void StatusBar::setQueueText (const juce::String& text)
    {
        queueText = text;
        repaint();
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.fillAll (colour::bg);

        auto area = getLocalBounds();
        g.setColour (colour::line);
        g.fillRect (area.removeFromTop (1));

        area.reduce (theme::metric::sectionPadding, 0);
        g.setFont (Fonts::mono (theme::type::fieldLabelSize));

        const auto queueWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), queueText);
        g.setColour (colour::muted);
        g.drawText (queueText, area.removeFromRight (queueWidth), juce::Justification::centredRight, false);
        area.removeFromRight (theme::metric::sectionPadding);

        auto textColour = colour::faint;

        if (tone != Tone::normal)
        {
            textColour = tone == Tone::warning ? colour::warn : colour::error;
            const auto badge = area.removeFromLeft (badgeSize).withSizeKeepingCentre (badgeSize, badgeSize);
            drawBadge (g, badge.toFloat(), "!", textColour);
            area.removeFromLeft (theme::metric::grid);
        }

        g.setColour (textColour);
        g.drawText (message, area, juce::Justification::centredLeft, true);
    }
}
