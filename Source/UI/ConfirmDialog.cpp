#include "ConfirmDialog.h"
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
        constexpr int panelWidth   = 520;
        constexpr int padding      = 24;
        constexpr int titleHeight  = 20;
        constexpr int gap          = 16;
        constexpr int itemHeight   = 24;
        constexpr int listPadding  = 8;
        constexpr int buttonGap    = 24;
        constexpr int confirmWidth = 144;
        constexpr int cancelWidth  = 96;
        constexpr int badgeSize    = 14;
        constexpr float backdropAlpha = 0.72f;
    }

    ConfirmDialog::ConfirmDialog()
    {
        setButtonStyle (confirmButton, ButtonStyle::primary);
        setButtonStyle (cancelButton, ButtonStyle::secondary);

        confirmButton.onClick = [this] { dismiss (true); };
        cancelButton.onClick  = [this] { dismiss (false); };

        addAndMakeVisible (confirmButton);
        addAndMakeVisible (cancelButton);

        setWantsKeyboardFocus (true);
        setVisible (false);
    }

    void ConfirmDialog::show (Content newContent, std::function<void (bool)> onResult)
    {
        content = std::move (newContent);
        callback = std::move (onResult);

        confirmButton.setButtonText (content.confirmText);
        cancelButton.setButtonText (content.cancelText);
        confirmButton.setTooltip (content.confirmText + " (Return)");
        cancelButton.setTooltip (content.cancelText + " (Escape)");

        setVisible (true);
        toFront (true);
        resized();
        repaint();
        grabKeyboardFocus();
    }

    void ConfirmDialog::dismiss (bool confirmed)
    {
        if (! isVisible())
            return;

        setVisible (false);

        if (auto done = std::move (callback); done != nullptr)
            done (confirmed);
    }

    bool ConfirmDialog::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
            dismiss (false);
        else if (key == juce::KeyPress::returnKey)
            dismiss (true);

        return true;    // modal: nothing reaches the window behind it
    }

    juce::TextLayout ConfirmDialog::layoutText (const juce::String& text, int width, juce::Colour c) const
    {
        juce::AttributedString s;
        s.append (text, Fonts::sans (type::bodySize), c);
        s.setLineSpacing (3.0f);

        juce::TextLayout layout;
        layout.createLayout (s, (float) width);
        return layout;
    }

    juce::Rectangle<int> ConfirmDialog::getPanelBounds() const
    {
        const auto width = juce::jmin (panelWidth, getWidth() - 4 * metric::grid);
        const auto inner = width - 2 * padding;

        auto height = padding + titleHeight + gap;

        if (content.intro.isNotEmpty())
            height += (int) std::ceil (layoutText (content.intro, inner, colour::text).getHeight()) + gap;

        if (! content.items.isEmpty())
            height += 2 * listPadding + content.items.size() * itemHeight + gap;

        if (content.note.isNotEmpty())
            height += (int) std::ceil (layoutText (content.note, inner, colour::faint).getHeight());

        height += buttonGap + metric::controlHeight + padding;

        return getLocalBounds().withSizeKeepingCentre (width, height);
    }

    void ConfirmDialog::resized()
    {
        auto buttons = getPanelBounds().reduced (padding).removeFromBottom (metric::controlHeight);
        confirmButton.setBounds (buttons.removeFromRight (confirmWidth));
        buttons.removeFromRight (metric::grid);
        cancelButton.setBounds (buttons.removeFromRight (cancelWidth));
    }

    void ConfirmDialog::paint (juce::Graphics& g)
    {
        // Dim the window behind (flat, no gradient), then a sharp panel with a warn border.
        g.fillAll (colour::bg.withAlpha (backdropAlpha));

        const auto panel = getPanelBounds();
        g.setColour (colour::panel);
        g.fillRect (panel);
        g.setColour (colour::warn);
        g.drawRect (panel, 1);

        auto area = panel.reduced (padding);

        // Title: warning badge + mono heading.
        auto title = area.removeFromTop (titleHeight);
        drawBadge (g, title.removeFromLeft (badgeSize).withSizeKeepingCentre (badgeSize, badgeSize).toFloat(), "!", colour::warn);
        title.removeFromLeft (metric::grid + 2);
        g.setColour (colour::heading);
        g.setFont (Fonts::mono (type::bodySize, FontWeight::bold));
        g.drawText (content.title, title, juce::Justification::centredLeft, true);
        area.removeFromTop (gap);

        if (content.intro.isNotEmpty())
        {
            const auto layout = layoutText (content.intro, area.getWidth(), colour::text);
            const auto h = (int) std::ceil (layout.getHeight());
            layout.draw (g, area.removeFromTop (h).toFloat());
            area.removeFromTop (gap);
        }

        if (! content.items.isEmpty())
        {
            auto list = area.removeFromTop (2 * listPadding + content.items.size() * itemHeight);
            area.removeFromTop (gap);

            g.setColour (colour::bg);
            g.fillRect (list);
            g.setColour (colour::line);
            g.drawRect (list, 1);

            list.reduce (metric::grid + 4, listPadding);
            g.setFont (Fonts::mono (type::controlSize));

            for (const auto& item : content.items)
            {
                auto row = list.removeFromTop (itemHeight);
                g.setColour (colour::warn);
                g.fillRect (row.removeFromLeft (6).withSizeKeepingCentre (6, 6));
                row.removeFromLeft (metric::grid + 2);
                g.setColour (colour::heading);
                g.drawText (item, row, juce::Justification::centredLeft, true);
            }
        }

        if (content.note.isNotEmpty())
        {
            const auto layout = layoutText (content.note, area.getWidth(), colour::faint);
            layout.draw (g, area.removeFromTop ((int) std::ceil (layout.getHeight())).toFloat());
        }
    }
}
