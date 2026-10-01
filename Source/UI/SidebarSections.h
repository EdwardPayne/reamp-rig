#pragma once

#include "Meters.h"
#include "SidebarSection.h"

namespace rf::ui
{
    /*  The four sidebar sections. AUDIO (phase 3), DESTINATION and OPTIONS (phase 4) are
        live; SYNC shows the driver latency and gets its measurement in phase 5. The sections
        only own and lay out their controls; the app wires them to settings and the engine.
    */

    class AudioSection final : public SidebarSection
    {
    public:
        AudioSection();

        juce::ComboBox& getTypeBox() noexcept              { return typeBox; }
        juce::ComboBox& getOutputDeviceBox() noexcept      { return outputDeviceBox; }
        juce::ComboBox& getInputDeviceBox() noexcept       { return inputDeviceBox; }
        juce::ComboBox& getSampleRateBox() noexcept        { return sampleRateBox; }
        juce::ComboBox& getBufferSizeBox() noexcept        { return bufferSizeBox; }
        juce::ComboBox& getOutputChannelBox() noexcept     { return outputChannelBox; }
        juce::ComboBox& getInputChannelBox() noexcept      { return inputChannelBox; }
        LevelMeter& getOutputMeter() noexcept              { return outputMeter; }
        LevelMeter& getInputMeter() noexcept               { return inputMeter; }
        juce::Slider& getOutputLevel() noexcept            { return outputLevel; }
        ValueReadout& getPeakReadout() noexcept            { return peakReadout; }
        juce::TextButton& getAuditionButton() noexcept     { return auditionButton; }

        /** Shows the "not sample-synchronized, for testing only" line (separate in/out devices). */
        void setSplitDevicesNoticeVisible (bool);

        /** Switches the Audition button between "Audition" and "Stop". */
        void setAuditioning (bool);

    private:
        juce::ComboBox typeBox, outputDeviceBox, inputDeviceBox, sampleRateBox, bufferSizeBox,
                       outputChannelBox, inputChannelBox;
        NoticeLine splitNotice;
        LevelMeter outputMeter { "Out" }, inputMeter { "In" };
        juce::Slider outputLevel;
        ValueReadout peakReadout;
        juce::TextButton auditionButton { "Audition" };
    };

    class SyncSection final : public SidebarSection
    {
    public:
        SyncSection();

        /** Driver-reported input + output latency (reference for phase 5's measurement). */
        ValueReadout& getDriverReadout() noexcept   { return driver; }

    private:
        juce::Label hint;
        ValueReadout measured, driver;
        juce::TextButton syncButton { "Sync" };
    };

    class DestinationSection final : public SidebarSection
    {
    public:
        DestinationSection();

        juce::ToggleButton& getBesideSourceRadio() noexcept     { return besideSource; }
        juce::ToggleButton& getSingleFolderRadio() noexcept     { return singleFolder; }
        juce::TextEditor& getSubfolderEditor() noexcept         { return subfolderEditor; }
        PathField& getFolderField() noexcept                    { return folderField; }
        juce::ToggleButton& getMirrorToggle() noexcept          { return mirror; }
        juce::TextEditor& getPrefixEditor() noexcept            { return prefixEditor; }
        juce::TextEditor& getSuffixEditor() noexcept            { return suffixEditor; }
        ValueReadout& getExample() noexcept                     { return example; }
        juce::ComboBox& getFormatBox() noexcept                 { return formatBox; }
        juce::ComboBox& getCollisionBox() noexcept              { return collisionBox; }

        /** Shows the rows of one destination mode: the subfolder name, or the output folder
            with the mirror option. */
        void showSingleFolderRows (bool singleFolderMode);

    private:
        juce::ToggleButton besideSource { "Subfolder next to source" }, singleFolder { "Single output folder" };
        juce::TextEditor subfolderEditor, prefixEditor, suffixEditor;
        PathField folderField { "Choose a folder" };
        juce::ToggleButton mirror { "Mirror folder structure" };
        ValueReadout example;
        juce::ComboBox formatBox, collisionBox;
    };

    class OptionsSection final : public SidebarSection
    {
    public:
        OptionsSection();

        /** "Include subfolders" (persisted by the app through its onClick). */
        juce::ToggleButton& getIncludeSubfoldersToggle() noexcept   { return includeSubfolders; }
        juce::ToggleButton& getChannelTagToggle() noexcept          { return channelTag; }
        juce::TextEditor& getTailEditor() noexcept                  { return tailEditor; }

    private:
        juce::ToggleButton includeSubfolders { "Include subfolders" };
        juce::ToggleButton channelTag { "Append channel tag (_L / _R)" };
        juce::TextEditor tailEditor;
    };
}
