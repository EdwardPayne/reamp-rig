#pragma once

#include <juce_graphics/juce_graphics.h>

/*  Reamp Rig design tokens (PROMPT.md section 5).

    Every colour and base metric used by the UI comes from here. Components and the
    LookAndFeel must not hard-code their own colour values.
*/
namespace rf::ui::theme
{
    //==============================================================================
    // Colour tokens (exact values from the design table).
    namespace colour
    {
        inline const juce::Colour bg         { 0xff000000 }; // window background
        inline const juce::Colour panel      { 0xff0b0b0d }; // cards, list background, waveform background
        inline const juce::Colour panel2     { 0xff1a1a1a }; // hover rows, secondary surfaces
        inline const juce::Colour lineSoft   { 0xff232323 }; // subtle separators
        inline const juce::Colour line       { 0xff28282c }; // card and control borders (1 px)
        inline const juce::Colour lineStrong { 0xff7a7a7a }; // focused control border
        inline const juce::Colour text       { 0xffd7d7d9 }; // body text
        inline const juce::Colour heading    { 0xffeeeeee }; // headings and primary values
        inline const juce::Colour faint      { 0xffa8a8a8 }; // secondary labels
        inline const juce::Colour muted      { 0xff8d8d8d }; // disabled text, ruler ticks
        inline const juce::Colour accent     { 0xffff5a36 }; // primary fill, playhead, selection, progress
        inline const juce::Colour ok         { 0xffb4c96f }; // Done status, sync OK
        inline const juce::Colour warn       { 0xffffb000 }; // warnings, clip indicators, "not synced"
        inline const juce::Colour error      { 0xffff5a36 }; // errors (always paired with an icon/label)
    }

    //==============================================================================
    // Metrics. Spacing is on an 8 px grid; borders are 1 px; corners are always square.
    namespace metric
    {
        inline constexpr int   grid          = 8;
        inline constexpr float borderWidth   = 1.0f;
        inline constexpr float cornerRadius  = 0.0f;

        inline constexpr int topBarHeight    = 44;
        inline constexpr int sidebarWidth    = 280;
        inline constexpr int statusBarHeight = 24;
        inline constexpr int splitterSize    = 5;

        inline constexpr int waveformDefaultHeight = 220;
        inline constexpr int waveformMinHeight     = 140;
        inline constexpr int fileTreeMinHeight     = 160;

        inline constexpr int controlHeight   = 32;
        inline constexpr int fieldLabelHeight = 16;
        inline constexpr int sectionPadding  = 16;

        inline constexpr int minWindowWidth  = 1100;
        inline constexpr int minWindowHeight = 700;
    }

    //==============================================================================
    // Type scale (point sizes, equivalent to CSS px) and letter-spacing (fraction of em).
    namespace type
    {
        inline constexpr float sectionLabelSize     = 11.0f;
        inline constexpr float sectionLabelTracking = 0.18f;

        inline constexpr float buttonSize           = 11.0f;
        inline constexpr float buttonTracking       = 0.08f;

        inline constexpr float brandSize            = 12.0f;
        inline constexpr float brandTracking        = 0.04f;

        inline constexpr float chipSize             = 10.0f;
        inline constexpr float chipTracking         = 0.04f;

        inline constexpr float fieldLabelSize       = 11.0f;
        inline constexpr float controlSize          = 12.0f;
        inline constexpr float bodySize             = 13.0f;
        inline constexpr float helpSize             = 12.0f;
        inline constexpr float rulerSize            = 10.0f;
    }
}
