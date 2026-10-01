#include "SidebarSection.h"
#include "Fonts.h"
#include "LookAndFeel.h"
#include "Theme.h"

#include <algorithm>

namespace rf::ui
{
    namespace colour = theme::colour;
    namespace metric = theme::metric;
    namespace type   = theme::type;

    namespace
    {
        constexpr int titleHeight    = 16;
        constexpr int labelGap       = 4;
        constexpr int rowGap         = metric::grid;
    }

    //==============================================================================
    bool SidebarSection::Row::hasLabels() const
    {
        return std::any_of (fields.begin(), fields.end(), [] (const Field& f) { return f.label.isNotEmpty(); });
    }

    int SidebarSection::Row::getHeight() const
    {
        return (hasLabels() ? metric::fieldLabelHeight + labelGap : 0) + controlHeight;
    }

    //==============================================================================
    SidebarSection::SidebarSection (juce::String sectionTitle)
        : title (std::move (sectionTitle))
    {
    }

    void SidebarSection::addRow (std::initializer_list<Field> fields, int controlHeight)
    {
        Row row;
        row.fields.assign (fields.begin(), fields.end());
        row.controlHeight = controlHeight;

        for (const auto& field : row.fields)
        {
            jassert (field.component != nullptr);
            addAndMakeVisible (field.component);

            auto label = std::make_unique<juce::Label> (juce::String(), field.label);
            label->setFont (Fonts::mono (type::fieldLabelSize));
            label->setColour (juce::Label::textColourId, colour::faint);
            label->setBorderSize (juce::BorderSize<int> (0));
            label->setJustificationType (juce::Justification::centredLeft);
            label->setInterceptsMouseClicks (false, false);
            addChildComponent (*label);
            label->setVisible (field.label.isNotEmpty());
            row.labels.push_back (std::move (label));
        }

        rows.push_back (std::move (row));
    }

    void SidebarSection::setRowVisible (juce::Component& component, bool shouldBeVisible)
    {
        for (auto& row : rows)
        {
            const auto contains = std::any_of (row.fields.begin(), row.fields.end(),
                                               [&] (const Field& f) { return f.component == &component; });

            if (! contains || row.visible == shouldBeVisible)
                continue;

            row.visible = shouldBeVisible;

            for (size_t i = 0; i < row.fields.size(); ++i)
            {
                row.fields[i].component->setVisible (shouldBeVisible);
                row.labels[i]->setVisible (shouldBeVisible && row.fields[i].label.isNotEmpty());
            }

            resized();

            if (onPreferredHeightChanged != nullptr)
                onPreferredHeightChanged();
        }
    }

    int SidebarSection::getPreferredHeight() const
    {
        auto height = metric::sectionPadding + titleHeight + metric::grid + metric::sectionPadding;

        auto numVisible = 0;

        for (const auto& row : rows)
        {
            if (row.visible)
            {
                height += row.getHeight() + rowGap;
                ++numVisible;
            }
        }

        return numVisible == 0 ? height : height - rowGap;
    }

    void SidebarSection::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();

        g.setColour (colour::line);
        g.fillRect (area.removeFromBottom (1));

        drawSectionLabel (g, title, area.reduced (metric::sectionPadding, 0)
                                        .withTrimmedTop (metric::sectionPadding)
                                        .withHeight (titleHeight));
    }

    void SidebarSection::resized()
    {
        auto area = getLocalBounds().withTrimmedBottom (1).reduced (metric::sectionPadding);
        area.removeFromTop (titleHeight + metric::grid);

        for (auto& row : rows)
        {
            if (! row.visible)
                continue;

            auto rowArea = area.removeFromTop (row.getHeight());
            area.removeFromTop (rowGap);

            const auto numFields = (int) row.fields.size();
            const auto fieldWidth = (rowArea.getWidth() - (numFields - 1) * metric::grid) / numFields;

            for (int i = 0; i < numFields; ++i)
            {
                auto fieldArea = i == numFields - 1 ? rowArea : rowArea.removeFromLeft (fieldWidth);
                rowArea.removeFromLeft (metric::grid);

                if (row.hasLabels())
                {
                    row.labels[(size_t) i]->setBounds (fieldArea.removeFromTop (metric::fieldLabelHeight));
                    fieldArea.removeFromTop (labelGap);
                }

                row.fields[(size_t) i].component->setBounds (fieldArea);
            }
        }
    }

    //==============================================================================
    ValueReadout::ValueReadout (juce::String k, juce::String v)
        : key (std::move (k)), value (std::move (v))
    {
    }

    void ValueReadout::setValue (const juce::String& newValue, std::optional<juce::Colour> colour)
    {
        if (newValue == value && colour == valueColour)
            return;

        value = newValue;
        valueColour = colour;
        repaint();
    }

    void ValueReadout::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();

        g.setColour (colour::faint);
        g.setFont (Fonts::mono (type::fieldLabelSize));
        g.drawText (key, area, juce::Justification::centredLeft, false);

        const auto keyWidth = key.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), key) + metric::grid;
        const auto valueFont = Fonts::mono (type::controlSize, FontWeight::medium);

        g.setColour (valueColour.value_or (colour::heading));
        g.setFont (valueFont);

        if (truncateStart)
            g.drawText (fitFromStart (valueFont, value, area.getWidth() - keyWidth), area.withTrimmedLeft (keyWidth),
                        juce::Justification::centredRight, false);
        else
            g.drawText (value, area, juce::Justification::centredRight, true);
    }

    //==============================================================================
    juce::String fitFromStart (const juce::Font& font, const juce::String& text, int width)
    {
        if (juce::GlyphArrangement::getStringWidthInt (font, text) <= width)
            return text;

        const auto ellipsis = utf8 ("\xe2\x80\xa6");

        for (int start = 1; start < text.length(); ++start)
        {
            const auto candidate = ellipsis + text.substring (start);

            if (juce::GlyphArrangement::getStringWidthInt (font, candidate) <= width)
                return candidate;
        }

        return ellipsis;
    }

    PathField::PathField (juce::String placeholderText)
        : placeholder (std::move (placeholderText))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void PathField::setPath (const juce::String& displayPath)
    {
        path = displayPath;
        repaint();
    }

    void PathField::mouseUp (const juce::MouseEvent& e)
    {
        if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
            onClick();
    }

    void PathField::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds();
        const auto hot = isEnabled() && isMouseOver (true);

        g.setColour (hot ? colour::panel2 : colour::panel);
        g.fillRect (bounds);
        g.setColour (! isEnabled() ? colour::lineSoft : hot ? colour::lineStrong : colour::line);
        g.drawRect (bounds, 1);

        auto area = bounds.reduced (metric::grid, 0);
        const auto font = Fonts::mono (type::controlSize);
        g.setFont (font);

        // "…" affordance on the right: the field opens a folder chooser.
        const auto more = utf8 ("\xe2\x80\xa6");
        const auto moreWidth = juce::GlyphArrangement::getStringWidthInt (font, more);
        g.setColour (colour::faint);
        g.drawText (more, area.removeFromRight (moreWidth), juce::Justification::centredRight, false);
        area.removeFromRight (metric::grid);

        if (path.isEmpty())
        {
            g.setColour (colour::muted);
            g.drawText (placeholder, area, juce::Justification::centredLeft, true);
            return;
        }

        g.setColour (isEnabled() ? colour::text : colour::muted);
        g.drawText (fitFromStart (font, path, area.getWidth()), area, juce::Justification::centredLeft, false);
    }

    //==============================================================================
    NoticeLine::NoticeLine (juce::String t)
        : text (std::move (t)), tone (colour::warn)
    {
    }

    void NoticeLine::setTone (juce::Colour c)
    {
        tone = c;
        repaint();
    }

    void NoticeLine::setText (const juce::String& t)
    {
        text = t;
        repaint();
    }

    void NoticeLine::paint (juce::Graphics& g)
    {
        constexpr int badgeSize = 12;
        auto area = getLocalBounds();

        // Badge aligned with the first line; the text may wrap onto a second line.
        drawBadge (g, area.removeFromLeft (badgeSize).removeFromTop (metric::fieldLabelHeight)
                          .withSizeKeepingCentre (badgeSize, badgeSize).toFloat(), "!", tone);
        area.removeFromLeft (metric::grid);

        g.setColour (tone);
        g.setFont (Fonts::mono (type::fieldLabelSize));
        g.drawFittedText (text, area, juce::Justification::topLeft, 2, 1.0f);
    }
}
