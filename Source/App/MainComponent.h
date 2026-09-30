#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../UI/FileTreeView.h"
#include "../UI/Sidebar.h"
#include "../UI/StatusBar.h"
#include "../UI/Theme.h"
#include "../UI/TopBar.h"
#include "../UI/WaveformPanel.h"

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
    */
    class MainComponent final : public juce::Component
    {
    public:
        MainComponent();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void layOutSplit (juce::Rectangle<int> area);

        ui::TopBar topBar;
        ui::FileTreeView fileTree;
        ui::WaveformPanel waveformPanel;
        ui::Sidebar sidebar;
        ui::StatusBar statusBar;

        juce::StretchableLayoutManager splitLayout;
        juce::StretchableLayoutResizerBar splitter { &splitLayout, 1, false };
        int waveformHeight = ui::theme::metric::waveformDefaultHeight;

        juce::TooltipWindow tooltipWindow { this, 600 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
