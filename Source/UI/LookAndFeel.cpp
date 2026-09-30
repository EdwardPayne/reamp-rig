#include "LookAndFeel.h"
#include "Theme.h"

namespace rf::ui
{
    namespace colour = theme::colour;

    namespace
    {
        const juce::Identifier buttonStyleProperty { "rfButtonStyle" };

        constexpr int popupItemHeight      = 28;
        constexpr int popupSeparatorHeight = 9;
        constexpr int popupTextIndent      = 24;
        constexpr int tickBoxSize          = 14;
        constexpr int tickBoxGap           = 8;
        constexpr int tooltipMaxWidth      = 320;

        void drawBorder (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour c)
        {
            g.setColour (c);
            g.drawRect (bounds, theme::metric::borderWidth);
        }

        /** A small open chevron, drawn with square line ends so it stays crisp. */
        void drawChevron (juce::Graphics& g, juce::Point<float> centre, float halfWidth,
                          bool pointsDown, juce::Colour c)
        {
            const auto halfHeight = halfWidth * 0.5f;
            const auto dir = pointsDown ? 1.0f : -1.0f;

            juce::Path p;
            p.startNewSubPath (centre.x - halfWidth, centre.y - dir * halfHeight);
            p.lineTo (centre.x, centre.y + dir * halfHeight);
            p.lineTo (centre.x + halfWidth, centre.y - dir * halfHeight);

            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        }

        void drawRightChevron (juce::Graphics& g, juce::Point<float> centre, float halfHeight, juce::Colour c)
        {
            const auto halfWidth = halfHeight * 0.5f;

            juce::Path p;
            p.startNewSubPath (centre.x - halfWidth, centre.y - halfHeight);
            p.lineTo (centre.x + halfWidth, centre.y);
            p.lineTo (centre.x - halfWidth, centre.y + halfHeight);

            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        }

        juce::TextLayout layoutTooltip (const juce::String& text, juce::Colour c)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.append (text, Fonts::sans (theme::type::helpSize), c);

            juce::TextLayout layout;
            layout.createLayoutWithBalancedLineLengths (s, (float) tooltipMaxWidth);
            return layout;
        }
    }

    //==============================================================================
    void setButtonStyle (juce::Button& button, ButtonStyle style)
    {
        button.getProperties().set (buttonStyleProperty, style == ButtonStyle::primary ? "primary" : "secondary");
        button.repaint();
    }

    ButtonStyle getButtonStyle (const juce::Button& button)
    {
        return button.getProperties()[buttonStyleProperty].toString() == "primary" ? ButtonStyle::primary
                                                                                  : ButtonStyle::secondary;
    }

    void styleTextEditor (juce::TextEditor& editor)
    {
        const auto font = Fonts::mono (theme::type::controlSize);
        editor.setFont (font);
        editor.applyFontToAllText (font);
        editor.setBorder (juce::BorderSize<int> (0));
        editor.setIndents (theme::metric::grid, 0);
        editor.setJustification (juce::Justification::centredLeft);
    }

    void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                           juce::Justification justification)
    {
        g.setFont (Fonts::sectionLabel());
        g.setColour (colour::faint);
        g.drawText (text.toUpperCase(), area, justification, false);
    }

    //==============================================================================
    ForgeLookAndFeel::ForgeLookAndFeel()
    {
        setColourScheme ({ colour::bg,        // windowBackground
                           colour::panel,     // widgetBackground
                           colour::panel,     // menuBackground
                           colour::line,      // outline
                           colour::text,      // defaultText
                           colour::accent,    // defaultFill
                           colour::bg,        // highlightedText
                           colour::panel2,    // highlightedFill
                           colour::text });   // menuText

        const auto transparent = juce::Colours::transparentBlack;

        const std::initializer_list<std::pair<int, juce::Colour>> colours
        {
            { juce::ResizableWindow::backgroundColourId,           colour::bg },
            { juce::DocumentWindow::textColourId,                  colour::heading },

            { juce::TextButton::buttonColourId,                    transparent },
            { juce::TextButton::buttonOnColourId,                  colour::panel2 },
            { juce::TextButton::textColourOffId,                   colour::text },
            { juce::TextButton::textColourOnId,                    colour::heading },

            { juce::ToggleButton::textColourId,                    colour::text },
            { juce::ToggleButton::tickColourId,                    colour::bg },
            { juce::ToggleButton::tickDisabledColourId,            colour::muted },

            { juce::ComboBox::backgroundColourId,                  colour::panel },
            { juce::ComboBox::outlineColourId,                     colour::line },
            { juce::ComboBox::focusedOutlineColourId,              colour::lineStrong },
            { juce::ComboBox::textColourId,                        colour::text },
            { juce::ComboBox::arrowColourId,                       colour::faint },
            { juce::ComboBox::buttonColourId,                      colour::panel },

            { juce::PopupMenu::backgroundColourId,                 colour::panel },
            { juce::PopupMenu::textColourId,                       colour::text },
            { juce::PopupMenu::headerTextColourId,                 colour::faint },
            { juce::PopupMenu::highlightedBackgroundColourId,      colour::panel2 },
            { juce::PopupMenu::highlightedTextColourId,            colour::heading },

            { juce::Slider::backgroundColourId,                    colour::line },
            { juce::Slider::trackColourId,                         colour::accent },
            { juce::Slider::thumbColourId,                         colour::heading },
            { juce::Slider::rotarySliderFillColourId,              colour::accent },
            { juce::Slider::rotarySliderOutlineColourId,           colour::line },
            { juce::Slider::textBoxTextColourId,                   colour::heading },
            { juce::Slider::textBoxBackgroundColourId,             transparent },
            { juce::Slider::textBoxHighlightColourId,              colour::accent.withAlpha (0.35f) },
            { juce::Slider::textBoxOutlineColourId,                transparent },

            { juce::Label::textColourId,                           colour::text },
            { juce::Label::backgroundColourId,                     transparent },
            { juce::Label::outlineColourId,                        transparent },
            { juce::Label::textWhenEditingColourId,                colour::heading },
            { juce::Label::backgroundWhenEditingColourId,          colour::panel },
            { juce::Label::outlineWhenEditingColourId,             colour::lineStrong },

            { juce::TextEditor::backgroundColourId,                colour::panel },
            { juce::TextEditor::textColourId,                      colour::heading },
            { juce::TextEditor::highlightColourId,                 colour::accent.withAlpha (0.35f) },
            { juce::TextEditor::highlightedTextColourId,           colour::heading },
            { juce::TextEditor::outlineColourId,                   colour::line },
            { juce::TextEditor::focusedOutlineColourId,            colour::lineStrong },
            { juce::TextEditor::shadowColourId,                    transparent },
            { juce::CaretComponent::caretColourId,                 colour::accent },

            { juce::ScrollBar::backgroundColourId,                 transparent },
            { juce::ScrollBar::trackColourId,                      transparent },
            { juce::ScrollBar::thumbColourId,                      colour::lineStrong },

            { juce::TooltipWindow::backgroundColourId,             colour::panel },
            { juce::TooltipWindow::textColourId,                   colour::text },
            { juce::TooltipWindow::outlineColourId,                colour::line },

            { juce::ProgressBar::backgroundColourId,               colour::panel },
            { juce::ProgressBar::foregroundColourId,               colour::accent },

            { juce::ListBox::backgroundColourId,                   colour::panel },
            { juce::ListBox::outlineColourId,                      colour::line },
            { juce::ListBox::textColourId,                         colour::text },

            { juce::TreeView::backgroundColourId,                  colour::panel },
            { juce::TreeView::linesColourId,                       colour::lineSoft },
            { juce::TreeView::selectedItemBackgroundColourId,      colour::panel2 },

            { juce::AlertWindow::backgroundColourId,               colour::panel },
            { juce::AlertWindow::textColourId,                     colour::text },
            { juce::AlertWindow::outlineColourId,                  colour::line },

            { juce::HyperlinkButton::textColourId,                 colour::accent },
        };

        for (const auto& [id, c] : colours)
            setColour (id, c);

        setDefaultSansSerifTypeface (fonts.getMonoTypeface (FontWeight::regular));
    }

    //==============================================================================
    juce::Typeface::Ptr ForgeLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        const auto style  = font.getTypefaceStyle();
        const auto weight = font.isBold() || style.containsIgnoreCase ("Bold") ? FontWeight::bold
                          : style.containsIgnoreCase ("Medium")                  ? FontWeight::medium
                                                                                 : FontWeight::regular;
        const auto& name = font.getTypefaceName();

        if (name == "Inter")
            return fonts.getSansTypeface (weight);

        // Any generic or platform default face is replaced by the embedded mono face so
        // that no system font can leak into the UI.
        if (name == "JetBrains Mono"
            || name == juce::Font::getDefaultSansSerifFontName()
            || name == juce::Font::getDefaultSerifFontName()
            || name == juce::Font::getDefaultMonospacedFontName()
            || name == juce::Font::getSystemUIFontName())
            return fonts.getMonoTypeface (weight);

        return LookAndFeel_V4::getTypefaceForFont (font);
    }

    //==============================================================================
    juce::Font ForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return Fonts::mono (theme::type::buttonSize, FontWeight::medium, theme::type::buttonTracking);
    }

    void ForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                 bool isHighlighted, bool isDown)
    {
        const auto bounds  = button.getLocalBounds().toFloat();
        const auto enabled = button.isEnabled();
        const auto focused = button.hasKeyboardFocus (false);

        if (getButtonStyle (button) == ButtonStyle::primary)
        {
            auto fill = colour::accent;

            if (! enabled)       fill = colour::panel2;
            else if (isDown)     fill = colour::accent.darker (0.25f);
            else if (isHighlighted) fill = colour::accent.brighter (0.12f);

            g.setColour (fill);
            g.fillRect (bounds);
            drawBorder (g, bounds, ! enabled ? colour::line : focused ? colour::lineStrong : fill);
            return;
        }

        auto fill = juce::Colours::transparentBlack;

        if (enabled && isDown)                                 fill = colour::line;
        else if (enabled && (isHighlighted || button.getToggleState())) fill = colour::panel2;

        g.setColour (fill);
        g.fillRect (bounds);
        drawBorder (g, bounds, ! enabled ? colour::lineSoft
                               : (focused || button.getToggleState()) ? colour::lineStrong
                                                                      : colour::line);
    }

    void ForgeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isHighlighted, bool)
    {
        const auto enabled = button.isEnabled();
        auto textColour = colour::muted;

        if (enabled)
            textColour = getButtonStyle (button) == ButtonStyle::primary ? colour::bg
                       : isHighlighted                                   ? colour::heading
                                                                         : colour::text;

        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (textColour);
        g.drawText (button.getButtonText().toUpperCase(),
                    button.getLocalBounds().reduced (theme::metric::grid, 0),
                    juce::Justification::centred, false);
    }

    //==============================================================================
    void ForgeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool isHighlighted, bool isDown)
    {
        const auto bounds = button.getLocalBounds();
        const auto boxY   = (float) (bounds.getHeight() - tickBoxSize) * 0.5f;

        drawTickBox (g, button, 0.0f, boxY, (float) tickBoxSize, (float) tickBoxSize,
                     button.getToggleState(), button.isEnabled(), isHighlighted, isDown);

        g.setFont (Fonts::mono (theme::type::controlSize));
        g.setColour (button.isEnabled() ? (isHighlighted ? colour::heading : colour::text) : colour::muted);
        g.drawText (button.getButtonText(),
                    bounds.withTrimmedLeft (tickBoxSize + tickBoxGap),
                    juce::Justification::centredLeft, true);
    }

    void ForgeLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component,
                                        float x, float y, float w, float h,
                                        bool ticked, bool isEnabled, bool isHighlighted, bool)
    {
        const juce::Rectangle<float> box (x, y, w, h);
        const auto focused = component.hasKeyboardFocus (false);

        if (ticked)
        {
            g.setColour (isEnabled ? colour::accent : colour::panel2);
            g.fillRect (box);

            juce::Path tick;
            tick.startNewSubPath (box.getX() + w * 0.22f, box.getY() + h * 0.52f);
            tick.lineTo (box.getX() + w * 0.42f, box.getY() + h * 0.72f);
            tick.lineTo (box.getX() + w * 0.78f, box.getY() + h * 0.30f);

            g.setColour (isEnabled ? colour::bg : colour::muted);
            g.strokePath (tick, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

            if (focused)
                drawBorder (g, box, colour::lineStrong);

            return;
        }

        g.setColour (isEnabled && isHighlighted ? colour::panel2 : colour::panel);
        g.fillRect (box);
        drawBorder (g, box, ! isEnabled ? colour::lineSoft
                            : (focused || isHighlighted) ? colour::lineStrong
                                                         : colour::line);
    }

    void ForgeLookAndFeel::changeToggleButtonWidthToFitText (juce::ToggleButton& button)
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (Fonts::mono (theme::type::controlSize),
                                                                          button.getButtonText());
        button.setSize (tickBoxSize + tickBoxGap + textWidth + theme::metric::grid, button.getHeight());
    }

    //==============================================================================
    void ForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int, int, int, int, juce::ComboBox& box)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);
        const auto enabled = box.isEnabled();
        const auto active  = isButtonDown || box.isPopupActive() || box.hasKeyboardFocus (true);

        g.setColour (enabled && box.isMouseOver (true) ? colour::panel2 : box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (bounds);

        drawBorder (g, bounds, ! enabled ? colour::lineSoft
                               : active  ? box.findColour (juce::ComboBox::focusedOutlineColourId)
                                         : box.findColour (juce::ComboBox::outlineColourId));

        drawChevron (g, { (float) width - 14.0f, (float) height * 0.5f }, 4.0f, ! box.isPopupActive(),
                     enabled ? box.findColour (juce::ComboBox::arrowColourId) : colour::muted);
    }

    juce::Font ForgeLookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return Fonts::mono (theme::type::controlSize);
    }

    void ForgeLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (1, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setBorderSize (juce::BorderSize<int> (0, theme::metric::grid - 1, 0, 0));
        label.setFont (getComboBoxFont (box));
    }

    void ForgeLookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
    {
        g.setColour (colour::muted);
        g.setFont (getComboBoxFont (box));
        g.drawText (box.getTextWhenNothingSelected(),
                    label.getBorderSize().subtractedFrom (label.getBounds()),
                    label.getJustificationType(), true);
    }

    //==============================================================================
    int ForgeLookAndFeel::getMenuWindowFlags()
    {
        return 0; // no drop shadow
    }

    int ForgeLookAndFeel::getPopupMenuBorderSize()
    {
        return 1;
    }

    juce::Font ForgeLookAndFeel::getPopupMenuFont()
    {
        return Fonts::mono (theme::type::controlSize);
    }

    void ForgeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        drawBorder (g, bounds, colour::line);
    }

    void ForgeLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                              bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                              bool hasSubMenu, const juce::String& text,
                                              const juce::String& shortcutKeyText,
                                              const juce::Drawable* icon, const juce::Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (colour::lineSoft);
            g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1));
            return;
        }

        auto textCol = textColour != nullptr ? *textColour : findColour (juce::PopupMenu::textColourId);

        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRect (area);
            g.setColour (colour::accent);
            g.fillRect (area.withWidth (2));
            textCol = findColour (juce::PopupMenu::highlightedTextColourId);
        }

        if (! isActive)
            textCol = colour::muted;

        const auto gutter = area.withWidth (popupTextIndent);

        if (icon != nullptr)
        {
            icon->drawWithin (g, gutter.reduced (6).toFloat(),
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
        }
        else if (isTicked)
        {
            g.setColour (isActive ? colour::accent : colour::muted);
            g.fillRect (gutter.withSizeKeepingCentre (6, 6));
        }

        auto textArea = area.withTrimmedLeft (popupTextIndent).withTrimmedRight (theme::metric::grid);

        if (hasSubMenu)
        {
            const auto arrowArea = textArea.removeFromRight (12);
            drawRightChevron (g, arrowArea.getCentre().toFloat(), 4.0f, textCol);
        }

        g.setFont (getPopupMenuFont());

        if (shortcutKeyText.isNotEmpty())
        {
            g.setColour (colour::muted);
            g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
        }

        g.setColour (textCol);
        g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);
    }

    void ForgeLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                       const juce::String& sectionName)
    {
        drawSectionLabel (g, sectionName, area.withTrimmedLeft (theme::metric::grid + 4));
    }

    void ForgeLookAndFeel::drawPopupMenuUpDownArrow (juce::Graphics& g, int width, int height, bool isScrollUpArrow)
    {
        g.setColour (findColour (juce::PopupMenu::backgroundColourId));
        g.fillRect (1, 1, width - 2, height - 2);
        drawChevron (g, { (float) width * 0.5f, (float) height * 0.5f }, 4.0f, ! isScrollUpArrow, colour::faint);
    }

    void ForgeLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                                      int& idealWidth, int& idealHeight)
    {
        if (isSeparator)
        {
            idealWidth  = 50;
            idealHeight = popupSeparatorHeight;
            return;
        }

        idealHeight = popupItemHeight;
        idealWidth  = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text)
                        + popupTextIndent + 3 * theme::metric::grid;
    }

    //==============================================================================
    int ForgeLookAndFeel::getSliderThumbRadius (juce::Slider&)
    {
        return 4;
    }

    void ForgeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        // Two- and three-value styles are not used by the app; they get the same flat single thumb.
        juce::ignoreUnused (minSliderPos, maxSliderPos, style);

        const auto enabled = slider.isEnabled();
        const auto fillColour = enabled ? slider.findColour (juce::Slider::trackColourId) : colour::muted;

        if (slider.isBar())
        {
            const juce::Rectangle<float> bounds ((float) x, (float) y, (float) width, (float) height);
            g.setColour (colour::panel);
            g.fillRect (bounds);

            g.setColour (fillColour);
            g.fillRect (slider.isHorizontal() ? bounds.withRight (sliderPos)
                                              : bounds.withTop (sliderPos));
            drawBorder (g, bounds, colour::line);
            return;
        }

        constexpr float trackThickness = 2.0f;
        constexpr float thumbLength    = 14.0f;
        constexpr float thumbThickness = 6.0f;

        const auto horizontal = slider.isHorizontal();
        const juce::Rectangle<float> area ((float) x, (float) y, (float) width, (float) height);

        const auto track = horizontal ? area.withSizeKeepingCentre (area.getWidth(), trackThickness)
                                      : area.withSizeKeepingCentre (trackThickness, area.getHeight());

        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillRect (track);

        g.setColour (fillColour);
        g.fillRect (horizontal ? track.withRight (sliderPos) : track.withTop (sliderPos));

        const auto thumb = horizontal
            ? juce::Rectangle<float> (thumbThickness, thumbLength).withCentre ({ sliderPos, track.getCentreY() })
            : juce::Rectangle<float> (thumbLength, thumbThickness).withCentre ({ track.getCentreX(), sliderPos });

        const auto hot = slider.isMouseOverOrDragging() || slider.hasKeyboardFocus (false);
        g.setColour (! enabled ? colour::muted : hot ? juce::Colours::white : slider.findColour (juce::Slider::thumbColourId));
        g.fillRect (thumb);
    }

    void ForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                                             juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const juce::PathStrokeType stroke (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (background, stroke);

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::rotarySliderFillColourId) : colour::muted);
        g.strokePath (value, stroke);

        const auto tip = centre.getPointOnCircumference (radius, angle);
        g.setColour (slider.isEnabled() ? colour::heading : colour::muted);
        g.drawLine ({ centre, tip }, 1.0f);
    }

    juce::Label* ForgeLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);

        label->setFont (Fonts::mono (theme::type::controlSize, FontWeight::medium));
        label->setJustificationType (juce::Justification::centredRight);
        label->setBorderSize (juce::BorderSize<int> (0));
        label->setColour (juce::Label::textColourId,               slider.findColour (juce::Slider::textBoxTextColourId));
        label->setColour (juce::Label::backgroundColourId,         juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId,            juce::Colours::transparentBlack);
        label->setColour (juce::Label::textWhenEditingColourId,    colour::heading);
        label->setColour (juce::Label::backgroundWhenEditingColourId, colour::panel);
        label->setColour (juce::Label::outlineWhenEditingColourId, colour::lineStrong);
        label->setColour (juce::TextEditor::textColourId,          colour::heading);
        label->setColour (juce::TextEditor::backgroundColourId,    colour::panel);
        label->setColour (juce::TextEditor::highlightColourId,     colour::accent.withAlpha (0.35f));

        return label;
    }

    //==============================================================================
    void ForgeLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
    {
        g.fillAll (label.findColour (juce::Label::backgroundColourId));

        if (! label.isBeingEdited())
        {
            const auto font = getLabelFont (label);
            const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());

            g.setColour (label.isEnabled() ? label.findColour (juce::Label::textColourId) : colour::muted);
            g.setFont (font);
            g.drawFittedText (label.getText(), area, label.getJustificationType(),
                              juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())),
                              label.getMinimumHorizontalScale());

            g.setColour (label.findColour (juce::Label::outlineColourId));
        }
        else
        {
            g.setColour (label.findColour (juce::Label::outlineWhenEditingColourId));
        }

        g.drawRect (label.getLocalBounds());
    }

    //==============================================================================
    void ForgeLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRect (0, 0, width, height);
    }

    void ForgeLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

        if (! editor.isEnabled())
            drawBorder (g, bounds, colour::lineSoft);
        else if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
            drawBorder (g, bounds, editor.findColour (juce::TextEditor::focusedOutlineColourId));
        else
            drawBorder (g, bounds, editor.findColour (juce::TextEditor::outlineColourId));
    }

    //==============================================================================
    int ForgeLookAndFeel::getDefaultScrollbarWidth()
    {
        return theme::metric::grid;
    }

    void ForgeLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width, int height,
                                          bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                          bool isMouseOver, bool isMouseDown)
    {
        g.setColour (scrollbar.findColour (juce::ScrollBar::trackColourId));
        g.fillRect (x, y, width, height);

        if (thumbSize <= 0)
            return;

        const auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x + 2, thumbStartPosition, width - 4, thumbSize)
                                               : juce::Rectangle<int> (thumbStartPosition, y + 2, thumbSize, height - 4);

        g.setColour (isMouseOver || isMouseDown ? colour::faint : scrollbar.findColour (juce::ScrollBar::thumbColourId));
        g.fillRect (thumb);
    }

    //==============================================================================
    juce::Rectangle<int> ForgeLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                            juce::Rectangle<int> parentArea)
    {
        const auto layout = layoutTooltip (tipText, colour::text);
        const auto w = (int) std::ceil (layout.getWidth())  + 2 * theme::metric::grid;
        const auto h = (int) std::ceil (layout.getHeight()) + theme::metric::grid + 4;

        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                                     w, h)
                   .constrainedWithin (parentArea);
    }

    void ForgeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRect (bounds);
        drawBorder (g, bounds, findColour (juce::TooltipWindow::outlineColourId));

        layoutTooltip (text, findColour (juce::TooltipWindow::textColourId))
            .draw (g, bounds.reduced ((float) theme::metric::grid, (float) theme::metric::grid * 0.5f + 2.0f));
    }

    //==============================================================================
    void ForgeLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                                            double progress, const juce::String& textToShow)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

        g.setColour (bar.findColour (juce::ProgressBar::backgroundColourId));
        g.fillRect (bounds);

        if (progress >= 0.0 && progress <= 1.0)
        {
            g.setColour (bar.findColour (juce::ProgressBar::foregroundColourId));
            g.fillRect (bounds.withWidth (bounds.getWidth() * (float) progress));
        }

        drawBorder (g, bounds, colour::line);

        if (textToShow.isNotEmpty())
        {
            g.setColour (colour::heading);
            g.setFont (Fonts::mono (theme::type::fieldLabelSize, FontWeight::medium));
            g.drawText (textToShow, bounds, juce::Justification::centred, false);
        }
    }

    void ForgeLookAndFeel::drawStretchableLayoutResizerBar (juce::Graphics& g, int width, int height, bool isVerticalBar,
                                                            bool isMouseOver, bool isMouseDragging)
    {
        g.fillAll (colour::bg);

        g.setColour (isMouseDragging ? colour::accent : isMouseOver ? colour::lineStrong : colour::line);

        if (isVerticalBar)
            g.fillRect (width / 2, 0, 1, height);
        else
            g.fillRect (0, height / 2, width, 1);
    }
}
