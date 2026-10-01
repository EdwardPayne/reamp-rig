#pragma once

#include "Meters.h"
#include "SidebarSection.h"

namespace rf::ui
{
    /*  The four sidebar sections: AUDIO (phase 3), SYNC (phase 5), DESTINATION and OPTIONS
        (phase 4). The sections only own and lay out their controls; the app wires them to
        settings and the engine.
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

    /** Five cells filling as the sync repeats run, with "Repeat 2 / 5" beside them. */
    class RepeatProgress final : public juce::Component,
                                 public juce::SettableTooltipClient
    {
    public:
        /** `current` is the fraction (0..1) of the repeat in progress. */
        void setProgress (int repeatsDone, int repeatsTotal, double current);
        void paint (juce::Graphics&) override;

    private:
        int done = 0, total = 5;
        double fraction = 0.0;
    };

    /*  SYNC (phase 5, PROMPT.md 3.6): the bypass hint, the sync level, the stored measurement
        for the current configuration (round trip, returned peak, confidence, date), the
        driver's latency for reference, a failure line, the repeat progress while measuring,
        and the Sync button (Stop while measuring). The app fills the readouts.
    */
    class SyncSection final : public SidebarSection
    {
    public:
        SyncSection();

        juce::Slider& getLevelSlider() noexcept         { return level; }
        juce::TextButton& getSyncButton() noexcept      { return syncButton; }

        /** Deletes the stored measurement of the current configuration (after a confirmation). */
        juce::TextButton& getForgetButton() noexcept    { return forgetButton; }

        /** Whether the current configuration has a stored measurement (set with the readouts;
            the app enables Forget from it). */
        void setHasMeasurement (bool has) noexcept      { hasMeasurement = has; }
        bool getHasMeasurement() const noexcept         { return hasMeasurement; }

        ValueReadout& getMeasuredReadout() noexcept     { return measured; }
        ValueReadout& getPeakReadout() noexcept         { return peak; }
        ValueReadout& getConfidenceReadout() noexcept   { return confidence; }
        ValueReadout& getDateReadout() noexcept         { return date; }

        /** Driver-reported input + output latency (reference next to the measurement). */
        ValueReadout& getDriverReadout() noexcept       { return driver; }

        /** Measuring: the button reads "Stop" and the repeat progress shows. */
        void setMeasuring (bool);
        bool isMeasuring() const noexcept               { return measuring; }
        void setProgress (int repeatsDone, int repeatsTotal, double current);

        /** A failure line under the readouts (empty text hides it); `detail` is its tooltip. */
        void setFailure (const juce::String& text, const juce::String& detail, juce::Colour tone);
        const juce::String& getFailure() const noexcept { return failure.getText(); }

    private:
        juce::Label hint;
        juce::Slider level;
        ValueReadout measured, peak, confidence, date, driver;
        NoticeLine failure { {} };
        RepeatProgress progress;
        juce::TextButton syncButton { "Sync" }, forgetButton { "Forget" };
        bool measuring = false, hasMeasurement = false;
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

        /** Seconds the batch waits between files (phase 6). */
        juce::TextEditor& getPauseEditor() noexcept                 { return pauseEditor; }

    private:
        juce::ToggleButton includeSubfolders { "Include subfolders" };
        juce::ToggleButton channelTag { "Append channel tag (_L / _R)" };
        juce::TextEditor tailEditor, pauseEditor;
    };
}
