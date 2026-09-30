#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Fonts.h"

namespace rf::ui
{
    /** Builds a String from a UTF-8 literal (use for non-ASCII glyphs such as \xc2\xb7). */
    inline juce::String utf8 (const char* text)   { return juce::String (juce::CharPointer_UTF8 (text)); }

    /** Visual role of a button. Primary buttons are accent-filled with black text. */
    enum class ButtonStyle { primary, secondary };

    void setButtonStyle (juce::Button&, ButtonStyle);
    ButtonStyle getButtonStyle (const juce::Button&);

    /** Applies the standard mono control font and padding to a text editor. */
    void styleTextEditor (juce::TextEditor&);

    /** Draws a small uppercase, letter-spaced mono section label. */
    void drawSectionLabel (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
                           juce::Justification = juce::Justification::centredLeft);

    //==============================================================================
    /*  The app-wide LookAndFeel: terminal-inspired, black, sharp-cornered, 1 px borders,
        no gradients and no shadows. All colours come from Theme.h.
    */
    class ForgeLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        ForgeLookAndFeel();

        //==============================================================================
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        //==============================================================================
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isHighlighted, bool isDown) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;
        void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                          bool ticked, bool isEnabled, bool isHighlighted, bool isDown) override;
        void changeToggleButtonWidthToFitText (juce::ToggleButton&) override;

        //==============================================================================
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawComboBoxTextWhenNothingSelected (juce::Graphics&, juce::ComboBox&, juce::Label&) override;

        //==============================================================================
        int getMenuWindowFlags() override;
        int getPopupMenuBorderSize() override;
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                                bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                const juce::String& text, const juce::String& shortcutKeyText,
                                const juce::Drawable* icon, const juce::Colour* textColour) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                         const juce::String& sectionName) override;
        void drawPopupMenuUpDownArrow (juce::Graphics&, int width, int height, bool isScrollUpArrow) override;
        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                        int& idealWidth, int& idealHeight) override;

        //==============================================================================
        int getSliderThumbRadius (juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                               juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;

        //==============================================================================
        void drawLabel (juce::Graphics&, juce::Label&) override;

        //==============================================================================
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

        //==============================================================================
        int getDefaultScrollbarWidth() override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                            bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                            bool isMouseOver, bool isMouseDown) override;

        //==============================================================================
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

        //==============================================================================
        void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height,
                              double progress, const juce::String& textToShow) override;

        void drawStretchableLayoutResizerBar (juce::Graphics&, int width, int height, bool isVerticalBar,
                                              bool isMouseOver, bool isMouseDragging) override;

    private:
        Fonts fonts;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeLookAndFeel)
    };
}
