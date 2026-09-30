#include "StatusBar.h"
#include "Fonts.h"
#include "Theme.h"

namespace rf::ui
{
    namespace colour = theme::colour;

    StatusBar::StatusBar()
    {
        setStatus ("Ready", "0 files queued");
    }

    void StatusBar::setStatus (const juce::String& left, const juce::String& right)
    {
        leftText = left;
        rightText = right;
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

        g.setColour (colour::faint);
        g.drawText (leftText, area, juce::Justification::centredLeft, true);

        g.setColour (colour::muted);
        g.drawText (rightText, area, juce::Justification::centredRight, true);
    }
}
