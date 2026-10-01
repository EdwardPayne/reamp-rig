#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Model/OutputNaming.h"
#include "../UI/SidebarSections.h"
#include "Settings.h"

namespace rf::app
{
    /*  Binds the DESTINATION and OPTIONS sections to the settings (PROMPT.md 3.4, 3.7):
        every control shows the persisted value at startup and saves each change at once.
        Text fields save on every edit; an empty subfolder name falls back to "Reamped".
        The example line follows the lead file (or a stereo "Riff 01.wav" when none is
        selected) and the destination mode.

        The batch reads the current choices from here. A development run can override the
        destination for itself (--batch-check) without touching the saved settings.

        Message thread only.
    */
    class OutputOptions
    {
    public:
        OutputOptions (Settings&, ui::DestinationSection&, ui::OptionsSection&);
        ~OutputOptions();

        model::NamingOptions getNamingOptions() const;
        int getBitsPerSample() const;
        int getTailMs() const;

        /** The file whose name the example line shows (empty: a stereo "Riff 01.wav"). */
        void setExampleSource (const juce::File& source, const juce::File& root, std::optional<model::Channel>);

        /** For this run only: write everything into `folder` (single folder, mirrored). */
        void overrideDestination (const juce::File& folder);

        /** Disables the controls while a batch runs (the batch uses the values at its start
            for each file; changing them halfway would mix names). */
        void setEnabled (bool);

    private:
        void load();
        void wire();
        void chooseFolder();
        void refresh();

        Settings& settings;
        ui::DestinationSection& destination;
        ui::OptionsSection& options;

        std::optional<juce::File> overrideFolder;
        juce::File exampleSource, exampleRoot;
        std::optional<model::Channel> exampleChannel;
        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputOptions)
    };
}
