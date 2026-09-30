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
        constexpr int sliderTextBoxWidth = 72;

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
        : SidebarSection ("Audio")
    {
        fillCombo (deviceBox,        { "No device" },  "Audio device used for playback and recording.");
        fillCombo (outputChannelBox, { emDash() },       "Output channel that feeds the amp.");
        fillCombo (inputChannelBox,  { emDash() },       "Input channel that records the amp.");

        outputLevel.setSliderStyle (juce::Slider::LinearHorizontal);
        outputLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, sliderTextBoxWidth, sliderHeight);
        outputLevel.setRange (-60.0, 12.0, 0.1);
        outputLevel.setValue (0.0, juce::dontSendNotification);
        outputLevel.setDoubleClickReturnValue (true, 0.0);
        outputLevel.setTextValueSuffix (" dB");
        outputLevel.setNumDecimalPlacesToDisplay (1);
        outputLevel.setTooltip ("Gain applied to every file on playback (-60 to +12 dB).");

        setButtonStyle (auditionButton, ButtonStyle::secondary);
        auditionButton.setTooltip ("Play the selected file through the output without recording.");

        addRow ({ { "Device", &deviceBox } }, metric::controlHeight);
        addRow ({ { "Output", &outputChannelBox }, { "Input", &inputChannelBox } }, metric::controlHeight);
        addRow ({ { "Output level", &outputLevel } }, sliderHeight);
        addRow ({ { {}, &auditionButton } }, metric::controlHeight);
    }

    //==============================================================================
    SyncSection::SyncSection()
        : SidebarSection ("Sync"),
          measured ("Measured", emDash()),
          driver   ("Driver",   emDash())
    {
        hint.setText ("Connect the output directly to the input (bypass the amp) before measuring.",
                      juce::dontSendNotification);
        hint.setFont (Fonts::sans (theme::type::helpSize));
        hint.setColour (juce::Label::textColourId, theme::colour::faint);
        hint.setJustificationType (juce::Justification::topLeft);
        hint.setBorderSize (juce::BorderSize<int> (0));
        hint.setMinimumHorizontalScale (1.0f);

        measured.setTooltip ("Round-trip latency measured for this device configuration.");
        driver.setTooltip ("Input + output latency reported by the driver.");

        setButtonStyle (syncButton, ButtonStyle::primary);
        syncButton.setTooltip ("Measure the round-trip latency of the current output/input pair.");

        addRow ({ { {}, &hint } }, hintHeight);
        addRow ({ { {}, &measured } }, readoutHeight);
        addRow ({ { {}, &driver } }, readoutHeight);
        addRow ({ { {}, &syncButton } }, metric::controlHeight);
    }

    //==============================================================================
    DestinationSection::DestinationSection()
        : SidebarSection ("Destination"),
          example ("Example", "Reamped/Riff 01_reamp.wav")
    {
        fillCombo (modeBox,      { "Subfolder next to source", "Single output folder" },
                   "Where processed files are written.");
        fillCombo (formatBox,    { utf8 ("WAV \xc2\xb7 16-bit"), utf8 ("WAV \xc2\xb7 24-bit"), utf8 ("WAV \xc2\xb7 32-bit float") },
                   "Output file format. The sample rate always matches the source.");
        fillCombo (collisionBox, { "Auto-number", "Overwrite", "Skip" },
                   "What to do when the output file already exists.");
        formatBox.setSelectedItemIndex (1, juce::dontSendNotification);

        setUpEditor (subfolderEditor, "Reamped", "Name of the subfolder created next to each source file.");
        setUpEditor (prefixEditor,    {},        "Text added before the original file name.");
        setUpEditor (suffixEditor,    "_reamp",  "Text added after the original file name.");

        example.setTooltip ("Resulting file name for a source called \"Riff 01.wav\".");

        addRow ({ { "Mode", &modeBox } }, metric::controlHeight);
        addRow ({ { "Subfolder", &subfolderEditor } }, metric::controlHeight);
        addRow ({ { "Prefix", &prefixEditor }, { "Suffix", &suffixEditor } }, metric::controlHeight);
        addRow ({ { {}, &example } }, readoutHeight);
        addRow ({ { "Format", &formatBox }, { "Collision", &collisionBox } }, metric::controlHeight);
    }

    //==============================================================================
    OptionsSection::OptionsSection()
        : SidebarSection ("Options")
    {
        includeSubfolders.setToggleState (true, juce::dontSendNotification);
        includeSubfolders.setTooltip ("Scan dropped and added folders recursively.");
        channelTag.setTooltip ("Append _L or _R to output names of stereo sources.");

        setUpEditor (tailEditor, "0", "Extra time recorded after the source ends, in milliseconds.");
        tailEditor.setInputRestrictions (6, "0123456789");

        addRow ({ { {}, &includeSubfolders } }, toggleHeight);
        addRow ({ { {}, &channelTag } }, toggleHeight);
        addRow ({ { "Tail (ms)", &tailEditor } }, metric::controlHeight);
    }
}
