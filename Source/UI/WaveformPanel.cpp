#include "WaveformPanel.h"
#include "Fonts.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace rf::ui
{
    namespace colour = theme::colour;
    namespace metric = theme::metric;
    namespace type   = theme::type;

    namespace
    {
        constexpr int headerHeight = 32;
        constexpr int rulerHeight  = 20;
        constexpr int laneLabelWidth = 112;
        constexpr int majorTickSpacing = 80;
        constexpr int minorTicksPerMajor = 4;
    }

    void WaveformPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colour::panel);

        auto area = getLocalBounds();

        // Header: section label + transport readout.
        auto header = area.removeFromTop (headerHeight);
        g.setColour (colour::line);
        g.fillRect (header.removeFromBottom (1));
        header.reduce (metric::sectionPadding, 0);

        drawSectionLabel (g, "Waveform", header);

        g.setColour (colour::heading);
        g.setFont (Fonts::mono (type::controlSize, FontWeight::medium));
        g.drawText ("00:00.000 / 00:00.000", header, juce::Justification::centredRight, false);

        paintRuler (g, area.removeFromTop (rulerHeight));

        const auto laneHeight = area.getHeight() / 2;
        paintLane (g, area.removeFromTop (laneHeight), "Source", "No file selected");

        g.setColour (colour::lineSoft);
        g.fillRect (area.removeFromTop (1));

        paintLane (g, area, "Recorded", utf8 ("\xe2\x80\x94"));
    }

    void WaveformPanel::paintRuler (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        g.setColour (colour::lineSoft);
        g.fillRect (area.removeFromBottom (1));

        const auto x0 = area.getX() + laneLabelWidth;
        const auto minorSpacing = majorTickSpacing / minorTicksPerMajor;

        g.setFont (Fonts::mono (type::rulerSize));

        for (int x = x0, i = 0; x < area.getRight(); x += minorSpacing, ++i)
        {
            const auto isMajor = (i % minorTicksPerMajor) == 0;
            g.setColour (colour::muted);
            g.fillRect (x, area.getBottom() - (isMajor ? 8 : 4), 1, isMajor ? 8 : 4);

            if (isMajor && x + majorTickSpacing <= area.getRight())
                g.drawText (juce::String (i / minorTicksPerMajor) + "s",
                            x + 4, area.getY(), majorTickSpacing - 8, area.getHeight() - 6,
                            juce::Justification::centredLeft, false);
        }
    }

    void WaveformPanel::paintLane (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name,
                                   const juce::String& emptyText) const
    {
        auto labelArea = area.removeFromLeft (laneLabelWidth);

        g.setColour (colour::lineSoft);
        g.fillRect (labelArea.removeFromRight (1));
        drawSectionLabel (g, name, labelArea.reduced (metric::sectionPadding, metric::grid),
                          juce::Justification::topLeft);

        // Centre (zero) line.
        g.setColour (colour::lineSoft);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1));

        g.setColour (colour::muted);
        g.setFont (Fonts::mono (type::controlSize));
        g.drawText (emptyText, area.withSizeKeepingCentre (area.getWidth(), 20).translated (0, -14),
                    juce::Justification::centred, false);
    }
}
