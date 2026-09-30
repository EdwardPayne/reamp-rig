#pragma once

#include "SidebarSection.h"

namespace rf::ui
{
    /*  The four sidebar sections. Apart from "Include subfolders" (phase 2) the controls are
        still placeholders; they are connected to settings and the engine in phases 3 and 4.
    */

    class AudioSection final : public SidebarSection
    {
    public:
        AudioSection();

    private:
        juce::ComboBox deviceBox, outputChannelBox, inputChannelBox;
        juce::Slider outputLevel;
        juce::TextButton auditionButton { "Audition" };
    };

    class SyncSection final : public SidebarSection
    {
    public:
        SyncSection();

    private:
        juce::Label hint;
        ValueReadout measured, driver;
        juce::TextButton syncButton { "Sync" };
    };

    class DestinationSection final : public SidebarSection
    {
    public:
        DestinationSection();

    private:
        juce::ComboBox modeBox, formatBox, collisionBox;
        juce::TextEditor subfolderEditor, prefixEditor, suffixEditor;
        ValueReadout example;
    };

    class OptionsSection final : public SidebarSection
    {
    public:
        OptionsSection();

        /** "Include subfolders" (persisted by the app through its onClick). */
        juce::ToggleButton& getIncludeSubfoldersToggle() noexcept   { return includeSubfolders; }

    private:
        juce::ToggleButton includeSubfolders { "Include subfolders" };
        juce::ToggleButton channelTag { "Append channel tag (_L / _R)" };
        juce::TextEditor tailEditor;
    };
}
