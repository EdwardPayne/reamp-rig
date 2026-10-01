#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Model/FileTree.h"
#include "LookAndFeel.h"

namespace rf::ui
{
    /*  The file list, grouped by folder (PROMPT.md section 3.1).

        Header: "FILES", live file/selection count, "Add files…" and "Add folder…".
        Body: a column header, then collapsible folder groups (path + count) with one row per
        file: name, Mono/Stereo, duration, sample rate, bit depth, L/R selector, status and a
        progress bar. Everything is painted here with Theme tokens; no stock list component
        is used. Shows the drop hint while the list is empty.

        Mouse: click, shift-click (range), cmd-click (toggle); click a group header to collapse
        it (its files leave the selection, so bulk actions never touch hidden rows); right-click
        for the context menu. Keys while the list has focus: cmd-A (every visible file),
        Delete / Backspace, L, R, Up/Down (shift extends).

        Batch (phase 4): the file being recorded is highlighted (accent tint and edge), every
        row shows its status with warning badges (NC, RS, XR, SIL, CLIP; details in the
        tooltip) and its progress bar; progress-only changes repaint just that row. The
        context menu offers "Skip current file", "Redo files with warnings" and "Reset status";
        the header shows a "Redo warnings" button while files with redoable warnings exist.

        All state lives in the model::FileTree; this view only keeps collapse state, the
        shift-click anchor and hover.
    */
    class FileTreeView final : public juce::Component,
                               public juce::TooltipClient,
                               private model::FileTree::Listener
    {
    public:
        explicit FileTreeView (model::FileTree&);
        ~FileTreeView() override;

        std::function<void()> onAddFiles, onAddFolder;

        /** Batch actions offered by the context menu and the header (the app performs them). */
        std::function<void()> onSkipCurrent, onRedoWarnings;

        /** The files "Redo files with warnings" would queue again (the app decides: dropout,
            silence, clipping, and NC files whose configuration is synced by now). Without
            it, the files with a dropout, silence or clipping warning. */
        std::function<std::vector<model::ItemId>()> getRedoableIds;

        /** Re-checks whether the "Redo warnings" button shows (e.g. after a sync measurement
            was stored or forgotten). */
        void refreshRedoButton()                         { updateRedoButton(); }

        /** The file the batch is recording (0 = none) and whether a batch is active. */
        void setBatchState (model::ItemId current, bool active);

        /** Accent outline while files are dragged over the window. */
        void setDropHighlight (bool);

        /** Gives keyboard focus to the list so cmd-A, Delete, L and R work. */
        void focusList();

        /** The scrolling row component that handles mouse and keys (exposed for tests). */
        juce::Component& getListComponent() noexcept;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        juce::String getTooltip() override;

    private:
        class Rows;

        void fileTreeChanged() override;
        void fileProgressChanged (model::ItemId) override;
        void updateRowsSize();
        void updateRedoButton();
        int countRedoable() const;
        juce::String getCountText() const;

        model::FileTree& tree;
        std::unique_ptr<Rows> rows;
        TooltipViewport viewport { "Scroll the file list (or use the mouse wheel / trackpad)." };
        juce::TextButton addFilesButton, addFolderButton, redoButton { "Redo warnings" };
        bool dropHighlight = false;
        bool batchActive = false;
        model::ItemId scrolledLead = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileTreeView)
    };
}
