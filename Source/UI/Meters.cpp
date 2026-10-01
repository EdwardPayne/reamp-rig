#include "Meters.h"
#include "Fonts.h"
#include "LookAndFeel.h"
#include "Theme.h"

#include <cmath>

namespace rf::ui
{
    namespace colour = theme::colour;
    namespace metric = theme::metric;
    namespace type   = theme::type;

    namespace
    {
        constexpr int labelWidth    = 28;
        constexpr int readoutWidth  = 40;
        constexpr int clipWidth     = 36;
        constexpr float fallDbPerSecond = 24.0f;
        constexpr double holdMs = 1500.0;
        constexpr float tickDbs[] = { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f };
    }

    LevelMeter::LevelMeter (juce::String text)
        : label (std::move (text))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setMouseClickGrabsKeyboardFocus (false);
        setWantsKeyboardFocus (false);
    }

    void LevelMeter::push (float peak, bool clipped)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto elapsed = lastUpdateMs > 0.0 ? (float) ((now - lastUpdateMs) / 1000.0) : 0.0f;
        lastUpdateMs = now;

        const auto db = peak > 0.0f ? juce::jmax (floorDb, 20.0f * std::log10 (peak)) : floorDb;
        const auto previousDisplay = displayDb, previousHold = holdDb;

        displayDb = juce::jmax (db, displayDb - fallDbPerSecond * elapsed);

        if (db >= holdDb || now >= holdUntilMs)
        {
            holdDb = db >= holdDb ? db : juce::jmax (db, displayDb);
            holdUntilMs = now + holdMs;
        }

        const auto wasLatched = clipLatched;
        clipLatched = clipLatched || clipped;

        if (! juce::approximatelyEqual (previousDisplay, displayDb) || ! juce::approximatelyEqual (previousHold, holdDb)
            || wasLatched != clipLatched)
            repaint();
    }

    void LevelMeter::reset()
    {
        displayDb = holdDb = floorDb;
        lastUpdateMs = 0.0;
        repaint();
    }

    void LevelMeter::clearClip()
    {
        if (clipLatched)
        {
            clipLatched = false;
            repaint();
        }
    }

    void LevelMeter::mouseDown (const juce::MouseEvent&)
    {
        clearClip();
    }

    float LevelMeter::dbToProportion (float db) const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();

        // Label
        g.setColour (colour::faint);
        g.setFont (Fonts::mono (type::fieldLabelSize, FontWeight::medium, type::sectionLabelTracking * 0.5f));
        g.drawText (label.toUpperCase(), area.removeFromLeft (labelWidth), juce::Justification::centredLeft, false);

        // Clip box
        const auto clipArea = area.removeFromRight (clipWidth);
        area.removeFromRight (metric::grid);

        g.setFont (Fonts::mono (type::chipSize, FontWeight::bold, type::chipTracking));

        if (clipLatched)
        {
            g.setColour (colour::warn);
            g.fillRect (clipArea);
            g.setColour (colour::bg);
        }
        else
        {
            g.setColour (colour::line);
            g.drawRect (clipArea, 1);
            g.setColour (colour::muted);
        }

        g.drawText ("CLIP", clipArea, juce::Justification::centred, false);

        // Readout (peak hold)
        const auto readoutArea = area.removeFromRight (readoutWidth);
        area.removeFromRight (metric::grid);

        const auto silent = holdDb <= floorDb;
        g.setColour (silent ? colour::muted : colour::heading);
        g.setFont (Fonts::mono (type::controlSize, FontWeight::medium));
        g.drawText (silent ? utf8 ("-\xe2\x88\x9e") : juce::String (holdDb, 1), readoutArea,
                    juce::Justification::centredRight, false);

        // Bar
        const auto bar = area;
        g.setColour (colour::panel);
        g.fillRect (bar);

        const auto inner = bar.reduced (2);
        const auto fillWidth = juce::roundToInt ((float) inner.getWidth() * dbToProportion (displayDb));

        g.setColour (colour::text);
        g.fillRect (inner.withWidth (fillWidth));

        g.setColour (colour::lineSoft);

        for (auto db : tickDbs)
        {
            const auto x = inner.getX() + juce::roundToInt ((float) inner.getWidth() * dbToProportion (db));

            if (x > inner.getX() + fillWidth)
                g.fillRect (x, inner.getY(), 1, inner.getHeight());
        }

        if (! silent)
        {
            const auto x = inner.getX() + juce::jmin (inner.getWidth() - 2,
                                                      juce::roundToInt ((float) inner.getWidth() * dbToProportion (holdDb)));
            g.setColour (colour::heading);
            g.fillRect (x, inner.getY(), 2, inner.getHeight());
        }

        g.setColour (colour::line);
        g.drawRect (bar, 1);
    }
}
