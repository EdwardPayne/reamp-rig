#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Model/FileTree.h"
#include "../Model/FolderScanner.h"
#include "../UI/FileTreeView.h"
#include "../UI/Sidebar.h"
#include "../UI/StatusBar.h"
#include "../UI/Theme.h"
#include "../UI/TopBar.h"
#include "../UI/WaveformPanel.h"
#include "CommandLine.h"
#include "Settings.h"

namespace rf::app
{
    /*  Root content component of the main window.

        +--------------------------------------------------+
        | TopBar (44 px)                                   |
        +-------------------------------------+------------+
        | FileTreeView                        | Sidebar    |
        +------------- splitter --------------+ (280 px)   |
        | WaveformPanel (~220 px)             |            |
        +-------------------------------------+------------+
        | StatusBar (24 px)                                |
        +--------------------------------------------------+

        Owns the file list model and the folder scanner, accepts file/folder drops anywhere
        in the window, and keeps the waveform panel and status bar in step with the list.
    */
    class MainComponent final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                private model::FileTree::Listener
    {
    public:
        explicit MainComponent (Settings&);
        ~MainComponent() override;

        /** Scans files/folders off the message thread and adds the result to the list.
            `onAdded` runs on the message thread after the result has been added. */
        void addPaths (const juce::Array<juce::File>&, std::function<void()> onAdded = {});

        /** Development aid for --select / --audition-at / --view (see CommandLine.h). */
        void applyLaunchOptions (const LaunchOptions&);

        /** True while scans are pending or the selected file's waveform is still building. */
        bool isBusy() const;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;

    private:
        void fileTreeChanged() override;
        void handleScanResult (const model::ScanResult&);
        void chooseFiles (bool folders);
        void layOutSplit (juce::Rectangle<int> area);

        Settings& settings;

        model::FileTree fileModel;
        model::FolderScanner scanner;

        ui::TopBar topBar;
        ui::FileTreeView fileTree { fileModel };
        ui::WaveformPanel waveformPanel;
        ui::Sidebar sidebar;
        ui::StatusBar statusBar;

        juce::StretchableLayoutManager splitLayout;
        juce::StretchableLayoutResizerBar splitter { &splitLayout, 1, false };
        int waveformHeight = ui::theme::metric::waveformDefaultHeight;

        std::unique_ptr<juce::FileChooser> chooser;
        juce::TooltipWindow tooltipWindow { this, 600 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
