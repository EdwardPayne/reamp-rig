#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/JuceAudioDevice.h"
#include "../Model/FileTree.h"
#include "../Model/FolderScanner.h"
#include "../UI/ConfirmDialog.h"
#include "../UI/FileTreeView.h"
#include "../UI/Sidebar.h"
#include "../UI/StatusBar.h"
#include "../UI/Theme.h"
#include "../UI/TopBar.h"
#include "../UI/WaveformPanel.h"
#include "AudioController.h"
#include "BatchController.h"
#include "CommandLine.h"
#include "OutputOptions.h"
#include "Settings.h"
#include "SyncController.h"

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
        Owns the audio device and the AudioController (phase 3); Space toggles audition.
        Phase 4: the OutputOptions binding (DESTINATION, OPTIONS) and the BatchController
        (transport, per-file status, recorded lane); while a batch runs the waveform panel
        follows the batch instead of the selection. Phase 5: the SyncController (SYNC
        section) and the themed confirmation dialog, an overlay above everything else.
    */
    class MainComponent final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                private model::FileTree::Listener
    {
    public:
        /** --virtual-device and friends in `options` select the development loopback device
            instead of the audio hardware. */
        MainComponent (Settings&, const LaunchOptions& options);
        ~MainComponent() override;

        /** Scans files/folders off the message thread and adds the result to the list.
            `onAdded` runs on the message thread after the result has been added. */
        void addPaths (const juce::Array<juce::File>&, std::function<void()> onAdded = {});

        /** Opens the audio device and applies --open plus the development aids (see
            CommandLine.h). `onCheckDone` is called when --audition-check, --sync-check or
            --batch-check (after --sync-check when both are given) finishes. */
        void applyLaunchOptions (const LaunchOptions&, std::function<void (bool ok)> onCheckDone = {});

        /** True while scans are pending or the selected file's waveform is still building. */
        bool isBusy() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;

    private:
        void fileTreeChanged() override;
        void fileProgressChanged (model::ItemId) override {}
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
        ui::ConfirmDialog confirmDialog;

        juce::StretchableLayoutManager splitLayout;
        juce::StretchableLayoutResizerBar splitter { &splitLayout, 1, false };
        int waveformHeight = ui::theme::metric::waveformDefaultHeight;

        // Declared after the views they drive; the controller is destroyed first, which
        // detaches the audio callback before the device goes away.
        std::unique_ptr<engine::AudioDeviceInterface> audioDevice;
        std::unique_ptr<AudioController> audio;
        std::unique_ptr<SyncController> sync;
        std::unique_ptr<OutputOptions> outputOptions;
        std::unique_ptr<BatchController> batch;     // destroyed first: cancels a running take

        std::unique_ptr<juce::FileChooser> chooser;
        juce::TooltipWindow tooltipWindow { this, 600 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
