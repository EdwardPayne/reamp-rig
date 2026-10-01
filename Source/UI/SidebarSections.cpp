#include "SidebarSections.h"
#include "Fonts.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace rf::ui
{
    namespace metric = theme::metric;

    namespace
    {
        constexpr int sliderHeight   = 24;
        constexpr int readoutHeight  = 20;
        constexpr int toggleHeight   = 24;
        constexpr int hintHeight     = 34;
        constexpr int noticeHeight   = 32;   // two lines
        constexpr int meterHeight    = 18;
        constexpr int sliderTextBoxWidth = 72;
        constexpr int syncTextBoxWidth = 88;
        constexpr int forgetButtonWidth = 80;

        juce::String emDash()   { return utf8 ("\xe2\x80\x94"); }

        void fillCombo (juce::ComboBox& box, const juce::StringArray& items, const juce::String& tooltip)
        {
            box.addItemList (items, 1);
            box.setSelectedItemIndex (0, juce::dontSendNotification);
            box.setTooltip (tooltip);
        }

        void setUpEditor (juce::TextEditor& editor, const juce::String& text, const juce::String& tooltip)
        {
            styleTextEditor (editor);
            editor.setText (text, false);
            editor.setTextToShowWhenEmpty ("none", theme::colour::muted);
            editor.setTooltip (tooltip);
        }
    }

    //==============================================================================
    AudioSection::AudioSection()
        : SidebarSection ("Audio"),
          splitNotice ("Not sample-synchronized, for testing only"),
          peakReadout ("Peak at output", emDash())
    {
        fillCombo (typeBox,          { "No driver" },  "Audio driver (CoreAudio on macOS; ASIO or Windows Audio on Windows).");
        fillCombo (outputDeviceBox,  { "No device" },  "Device whose output feeds the amp.");
        fillCombo (inputDeviceBox,   { "No device" },  "Device whose input records the amp. Use the same device as the output "
                                                        "for sample-accurate results.");
        fillCombo (sampleRateBox,    { emDash() },     "Device sample rate. The batch switches it to each file's rate and back.");
        fillCombo (bufferSizeBox,    { emDash() },     "Device buffer size in samples. Each rate and buffer needs its own Sync.");
        fillCombo (outputChannelBox, { emDash() },     "Output channel that feeds the amp.");
        fillCombo (inputChannelBox,  { emDash() },     "Input channel that records the amp.");

        splitNotice.setTooltip ("Input and output are different devices with independent clocks, so recordings "
                                "cannot be sample-aligned. Fine for trying the app with the built-in mic and "
                                "speakers; use one interface for real work.");

        outputMeter.setTooltip ("Peak level sent to the output channel (dBFS). CLIP latches when full scale is "
                                "reached; click the meter to reset it.");
        inputMeter.setTooltip ("Peak level on the input channel (dBFS). CLIP latches when full scale is reached; "
                               "click the meter to reset it.");

        outputLevel.setSliderStyle (juce::Slider::LinearHorizontal);
        outputLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, sliderTextBoxWidth, sliderHeight);
        outputLevel.setRange (-60.0, 12.0, 0.1);
        outputLevel.setValue (0.0, juce::dontSendNotification);
        outputLevel.setDoubleClickReturnValue (true, 0.0);
        outputLevel.setTextValueSuffix (" dB");
        outputLevel.setNumDecimalPlacesToDisplay (1);
        setSliderTooltip (outputLevel, "Gain applied to every file on playback (-60 to +12 dB). Double-click for 0 dB.");

        peakReadout.setTooltip ("Peak of the selected file's played channel with the output level applied.");

        setButtonStyle (auditionButton, ButtonStyle::secondary);
        auditionButton.setTooltip ("Play the selected file from the audition marker through the output channel, "
                                   "without recording (Space).");

        addRow ({ { "Driver", &typeBox } }, metric::controlHeight);
        addRow ({ { "Output device", &outputDeviceBox } }, metric::controlHeight);
        addRow ({ { "Input device", &inputDeviceBox } }, metric::controlHeight);
        addRow ({ { {}, &splitNotice } }, noticeHeight);
        addRow ({ { "Sample rate", &sampleRateBox }, { "Buffer", &bufferSizeBox } }, metric::controlHeight);
        addRow ({ { "Output", &outputChannelBox }, { "Input", &inputChannelBox } }, metric::controlHeight);
        addRow ({ { {}, &outputMeter } }, meterHeight);
        addRow ({ { {}, &inputMeter } }, meterHeight);
        addRow ({ { "Output level", &outputLevel } }, sliderHeight);
        addRow ({ { {}, &peakReadout } }, readoutHeight);
        addRow ({ { {}, &auditionButton } }, metric::controlHeight);

        setRowVisible (splitNotice, false);
    }

    void AudioSection::setSplitDevicesNoticeVisible (bool shouldShow)
    {
        setRowVisible (splitNotice, shouldShow);
    }

    void AudioSection::setAuditioning (bool isAuditioning)
    {
        auditionButton.setButtonText (isAuditioning ? "Stop" : "Audition");
        setButtonStyle (auditionButton, isAuditioning ? ButtonStyle::primary : ButtonStyle::secondary);
        auditionButton.repaint();
    }

    //==============================================================================
    void RepeatProgress::setProgress (int repeatsDone, int repeatsTotal, double current)
    {
        done = repeatsDone;
        total = juce::jmax (1, repeatsTotal);
        fraction = juce::jlimit (0.0, 1.0, current);
        repaint();
    }

    void RepeatProgress::paint (juce::Graphics& g)
    {
        constexpr int cellWidth = 22, cellHeight = 10, cellGap = 4;
        auto area = getLocalBounds();

        const auto label = "Repeat " + juce::String (juce::jmin (done + 1, total)) + " / " + juce::String (total);
        g.setColour (theme::colour::faint);
        g.setFont (Fonts::mono (theme::type::fieldLabelSize));
        g.drawText (label, area, juce::Justification::centredLeft, false);

        auto cells = area.removeFromRight (total * cellWidth + (total - 1) * cellGap);

        for (int i = 0; i < total; ++i)
        {
            auto cell = cells.removeFromLeft (cellWidth).withSizeKeepingCentre (cellWidth, cellHeight);
            cells.removeFromLeft (cellGap);

            g.setColour (theme::colour::line);
            g.drawRect (cell, 1);

            const auto filled = i < done ? 1.0 : i == done ? fraction : 0.0;

            if (filled > 0.0)
            {
                g.setColour (theme::colour::accent);
                g.fillRect (cell.withWidth (juce::roundToInt (cell.getWidth() * filled)));
            }
        }
    }

    //==============================================================================
    SyncSection::SyncSection()
        : SidebarSection ("Sync"),
          measured   ("Measured",      emDash()),
          peak       ("Returned peak", emDash()),
          confidence ("Confidence",    emDash()),
          date       ("Measured on",   emDash()),
          driver     ("Driver",        emDash())
    {
        hint.setText ("Connect the output directly to the input (bypass the amp) before measuring.",
                      juce::dontSendNotification);
        hint.setFont (Fonts::sans (theme::type::helpSize));
        hint.setColour (juce::Label::textColourId, theme::colour::faint);
        hint.setJustificationType (juce::Justification::topLeft);
        hint.setBorderSize (juce::BorderSize<int> (0));
        hint.setMinimumHorizontalScale (1.0f);

        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, syncTextBoxWidth, sliderHeight);
        level.setRange (-60.0, 0.0, 0.5);
        level.setValue (-12.0, juce::dontSendNotification);
        level.setDoubleClickReturnValue (true, -12.0);
        level.setTextValueSuffix (" dBFS");
        level.setNumDecimalPlacesToDisplay (1);
        setSliderTooltip (level, "Peak level of the sync test signal at the output (-60 to 0 dBFS, default -12). "
                                 "The output level does not apply to it. Double-click for -12 dBFS.");

        measured.setTooltip ("Round trip measured for the current device configuration.");
        peak.setTooltip ("Peak level of the test signal as it came back on the input.");
        confidence.setTooltip ("How clearly and how repeatably the test signal was found.");
        date.setTooltip ("When the stored measurement for this configuration was made.");
        driver.setTooltip ("Input + output latency reported by the driver (reference only).");
        progress.setTooltip ("The test signal is played and recorded five times; the median is used.");

        setButtonStyle (syncButton, ButtonStyle::primary);
        syncButton.setTooltip ("Measure the round-trip latency of the current output/input pair.");

        setButtonStyle (forgetButton, ButtonStyle::secondary);
        forgetButton.setTooltip ("Delete the stored measurement for the current device configuration (asks first).");
        forgetButton.setEnabled (false);

        addRow ({ { {}, &hint } }, hintHeight);
        addRow ({ { "Sync level", &level } }, sliderHeight);
        addRow ({ { {}, &measured } }, readoutHeight);
        addRow ({ { {}, &peak } }, readoutHeight);
        addRow ({ { {}, &confidence } }, readoutHeight);
        addRow ({ { {}, &date } }, readoutHeight);
        addRow ({ { {}, &driver } }, readoutHeight);
        addRow ({ { {}, &failure } }, noticeHeight);
        addRow ({ { {}, &progress } }, readoutHeight);
        addRow ({ { {}, &syncButton }, { {}, &forgetButton, forgetButtonWidth } }, metric::controlHeight);

        setRowVisible (failure, false);
        setRowVisible (progress, false);
    }

    void SyncSection::setMeasuring (bool isMeasuring)
    {
        measuring = isMeasuring;
        syncButton.setButtonText (isMeasuring ? "Stop" : "Sync");
        setButtonStyle (syncButton, isMeasuring ? ButtonStyle::secondary : ButtonStyle::primary);
        syncButton.repaint();
        setRowVisible (progress, isMeasuring);

        if (isMeasuring)
            setFailure ({}, {}, theme::colour::warn);
    }

    void SyncSection::setProgress (int repeatsDone, int repeatsTotal, double current)
    {
        progress.setProgress (repeatsDone, repeatsTotal, current);
    }

    void SyncSection::setFailure (const juce::String& text, const juce::String& detail, juce::Colour tone)
    {
        failure.setText (text);
        failure.setTone (tone);
        failure.setTooltip (detail);
        setRowVisible (failure, text.isNotEmpty());
    }

    //==============================================================================
    DestinationSection::DestinationSection()
        : SidebarSection ("Destination"),
          example ("Example", "Reamped/Riff 01_reamp.wav")
    {
        constexpr int destinationRadioGroup = 1001;

        for (auto* radio : { &besideSource, &singleFolder })
            radio->setRadioGroupId (destinationRadioGroup, juce::dontSendNotification);

        besideSource.setToggleState (true, juce::dontSendNotification);
        besideSource.setTooltip ("Write each result into a subfolder next to its source file.");
        singleFolder.setTooltip ("Write every result into one folder you choose.");

        setUpEditor (subfolderEditor, "Reamped", "Name of the subfolder created next to each source file.");
        subfolderEditor.setTextToShowWhenEmpty ("Reamped", theme::colour::muted);
        folderField.setTooltip ("Folder that receives the processed files. Click to choose.");
        mirror.setTooltip ("Recreate the source folders (below the folder you added) inside the output folder.");
        mirror.setToggleState (true, juce::dontSendNotification);

        setUpEditor (prefixEditor, {},        "Text added before the original file name.");
        setUpEditor (suffixEditor, "_reamp",  "Text added after the original file name.");

        example.setTruncateFromStart (true);
        example.setTooltip ("Resulting name for the selected file (or for \"Riff 01.wav\").");

        fillCombo (formatBox,    { utf8 ("WAV \xc2\xb7 16-bit"), utf8 ("WAV \xc2\xb7 24-bit"), utf8 ("WAV \xc2\xb7 32 float") },
                   "Output file format. The sample rate always matches the source.");
        fillCombo (collisionBox, { "Auto-number", "Overwrite", "Skip" },
                   "When the output file already exists: add \" (2)\" to the name, replace it, or skip the source.");
        formatBox.setSelectedItemIndex (1, juce::dontSendNotification);

        addRow ({ { "Mode", &besideSource } }, toggleHeight);
        addRow ({ { {}, &singleFolder } }, toggleHeight);
        addRow ({ { "Subfolder", &subfolderEditor } }, metric::controlHeight);
        addRow ({ { "Output folder", &folderField } }, metric::controlHeight);
        addRow ({ { {}, &mirror } }, toggleHeight);
        addRow ({ { "Prefix", &prefixEditor }, { "Suffix", &suffixEditor } }, metric::controlHeight);
        addRow ({ { {}, &example } }, readoutHeight);
        addRow ({ { "Format", &formatBox }, { "Collision", &collisionBox } }, metric::controlHeight);

        showSingleFolderRows (false);
    }

    void DestinationSection::showSingleFolderRows (bool singleFolderMode)
    {
        setRowVisible (subfolderEditor, ! singleFolderMode);
        setRowVisible (folderField, singleFolderMode);
        setRowVisible (mirror, singleFolderMode);
    }

    //==============================================================================
    OptionsSection::OptionsSection()
        : SidebarSection ("Options")
    {
        includeSubfolders.setToggleState (true, juce::dontSendNotification);
        includeSubfolders.setTooltip ("Scan dropped and added folders with their subfolders. Subfolders named like the "
                                      "DESTINATION subfolder (\"Reamped\") hold results and are always skipped.");
        channelTag.setTooltip ("Append _L or _R to output names of stereo sources.");

        setUpEditor (tailEditor, "0", "Extra time recorded after the source ends, in milliseconds (0 = exactly "
                                      "the source length).");
        tailEditor.setInputRestrictions (5, "0123456789");

        setUpEditor (pauseEditor, "2", "Seconds the batch waits after each file before the next take starts, so amp "
                                       "and reverb tails die out (0 to 60, default 2).");
        pauseEditor.setTextToShowWhenEmpty ("0", theme::colour::muted);
        pauseEditor.setInputRestrictions (4, "0123456789.");

        addRow ({ { {}, &includeSubfolders } }, toggleHeight);
        addRow ({ { {}, &channelTag } }, toggleHeight);
        addRow ({ { "Tail (ms)", &tailEditor } }, metric::controlHeight);
        addRow ({ { "Pause between files (s)", &pauseEditor } }, metric::controlHeight);
    }
}
