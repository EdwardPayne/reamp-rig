#include "TopBar.h"
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
        constexpr int brandMarkSize = 10;
        constexpr int chipHeight    = 22;
    }

    //==============================================================================
    TopBar::SyncChip::SyncChip()
    {
        setTooltip ("Latency calibration status for the current device configuration.");
    }

    void TopBar::SyncChip::setStatus (const juce::String& text, juce::Colour c)
    {
        statusText = text;
        statusColour = c;
        repaint();
    }

    int TopBar::SyncChip::getIdealWidth() const
    {
        const auto font = Fonts::mono (type::chipSize, FontWeight::bold, type::chipTracking);
        return juce::GlyphArrangement::getStringWidthInt (font, statusText.toUpperCase()) + 2 * metric::grid + 2;
    }

    void TopBar::SyncChip::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (statusColour);
        g.drawRect (bounds, metric::borderWidth);

        g.setFont (Fonts::mono (type::chipSize, FontWeight::bold, type::chipTracking));
        g.drawText (statusText.toUpperCase(), getLocalBounds(), juce::Justification::centred, false);
    }

    //==============================================================================
    TopBar::TopBar()
    {
        deviceSummary = "No device";

        syncChip.setStatus ("No device", colour::muted);
        addAndMakeVisible (syncChip);

        setButtonStyle (startButton, ButtonStyle::primary);
        setButtonStyle (pauseButton, ButtonStyle::secondary);
        setButtonStyle (stopButton,  ButtonStyle::secondary);

        startButton.setTooltip ("Process every queued file from top to bottom.");
        pauseButton.setTooltip ("Pause after the current buffer; press again to resume.");
        stopButton.setTooltip ("Stop the batch. The current take is discarded.");

        for (auto* b : { &startButton, &pauseButton, &stopButton })
            addAndMakeVisible (b);
    }

    void TopBar::setDeviceSummary (const juce::String& summary, const juce::String& tooltip)
    {
        deviceTooltip = tooltip;

        if (summary != deviceSummary)
        {
            deviceSummary = summary;
            repaint (deviceArea);
        }
    }

    void TopBar::setSyncStatus (const juce::String& text, juce::Colour c, const juce::String& tooltip)
    {
        syncChip.setStatus (text, c);
        syncChip.setTooltip (tooltip);
        resized();
    }

    juce::String TopBar::getTooltip()
    {
        const auto mouse = getMouseXYRelative();
        return deviceArea.contains (mouse) ? deviceTooltip : juce::String();
    }

    void TopBar::paint (juce::Graphics& g)
    {
        g.fillAll (colour::bg);

        g.setColour (colour::line);
        g.fillRect (getLocalBounds().removeFromBottom (1));

        // Brand: accent square + app name.
        auto brand = brandArea;
        g.setColour (colour::accent);
        g.fillRect (brand.removeFromLeft (brandMarkSize).withSizeKeepingCentre (brandMarkSize, brandMarkSize));
        brand.removeFromLeft (metric::grid);

        g.setColour (colour::heading);
        g.setFont (Fonts::mono (type::brandSize, FontWeight::bold, type::brandTracking));
        g.drawText ("REAMP FORGE", brand, juce::Justification::centredLeft, false);

        // Separator + device summary.
        g.setColour (colour::line);
        g.fillRect (deviceArea.getX() - metric::grid * 2, metric::grid * 2 - 4, 1, getHeight() - metric::grid * 4 + 8);

        g.setColour (colour::faint);
        g.setFont (Fonts::mono (type::controlSize));
        g.drawText (deviceSummary, deviceArea, juce::Justification::centredLeft, true);
    }

    void TopBar::resized()
    {
        auto area = getLocalBounds().withTrimmedBottom (1).reduced (metric::sectionPadding, 0);
        const auto buttonHeight = metric::controlHeight;

        auto placeRight = [&] (juce::Component& c, int width, int height)
        {
            c.setBounds (area.removeFromRight (width).withSizeKeepingCentre (width, height));
            area.removeFromRight (metric::grid);
        };

        placeRight (stopButton,  72, buttonHeight);
        placeRight (pauseButton, 80, buttonHeight);
        placeRight (startButton, 88, buttonHeight);
        area.removeFromRight (metric::grid);
        placeRight (syncChip, syncChip.getIdealWidth(), chipHeight);

        const auto brandWidth = brandMarkSize + metric::grid
                              + juce::GlyphArrangement::getStringWidthInt (Fonts::mono (type::brandSize, FontWeight::bold, type::brandTracking),
                                                                            "REAMP FORGE");
        brandArea = area.removeFromLeft (brandWidth);
        area.removeFromLeft (metric::grid * 4);
        deviceArea = area;
    }
}
