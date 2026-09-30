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

    int SidebarSection::getPreferredHeight() const
    {
        auto height = metric::sectionPadding + titleHeight + metric::grid + metric::sectionPadding;

        for (const auto& row : rows)
            height += row.getHeight() + rowGap;

        return rows.empty() ? height : height - rowGap;
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

    void ValueReadout::setValue (const juce::String& newValue)
    {
        value = newValue;
        repaint();
    }

    void ValueReadout::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds();

        g.setColour (colour::faint);
        g.setFont (Fonts::mono (type::fieldLabelSize));
        g.drawText (key, area, juce::Justification::centredLeft, false);

        g.setColour (colour::heading);
        g.setFont (Fonts::mono (type::controlSize, FontWeight::medium));
        g.drawText (value, area, juce::Justification::centredRight, true);
    }
}
