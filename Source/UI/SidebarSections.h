#pragma once

#include "SidebarSection.h"

namespace rf::ui
{
    /*  The four sidebar sections. Phase 1: representative placeholder controls only, so the
        look-and-feel can be judged. Nothing is connected to settings or the engine yet.
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

    private:
        juce::ToggleButton includeSubfolders { "Include subfolders" };
        juce::ToggleButton channelTag { "Append channel tag (_L / _R)" };
        juce::TextEditor tailEditor;
    };
}
