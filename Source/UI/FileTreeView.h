#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Model/FileTree.h"

namespace rf::ui
{
    /*  The file list, grouped by folder (PROMPT.md section 3.1).

        Header: "FILES", live file/selection count, "Add files…" and "Add folder…".
        Body: a column header, then collapsible folder groups (path + count) with one row per
        file: name, Mono/Stereo, duration, sample rate, bit depth, L/R selector, status and a
        progress bar. Everything is painted here with Theme tokens; no stock list component
        is used. Shows the drop hint while the list is empty.

        Mouse: click, shift-click (range), cmd-click (toggle); click a group header to collapse
        it; right-click for the context menu. Keys while the list has focus: cmd-A, Delete /
        Backspace, L, R, Up/Down (shift extends).

        All state lives in the model::FileTree; this view only keeps collapse state, the
        shift-click anchor and hover.
    */
    class FileTreeView final : public juce::Component,
                               private model::FileTree::Listener
    {
    public:
        explicit FileTreeView (model::FileTree&);
        ~FileTreeView() override;

        std::function<void()> onAddFiles, onAddFolder;

        /** Accent outline while files are dragged over the window. */
        void setDropHighlight (bool);

        /** Gives keyboard focus to the list so cmd-A, Delete, L and R work. */
        void focusList();

        /** The scrolling row component that handles mouse and keys (exposed for tests). */
        juce::Component& getListComponent() noexcept;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        class Rows;

        void fileTreeChanged() override;
        void updateRowsSize();
        juce::String getCountText() const;

        model::FileTree& tree;
        std::unique_ptr<Rows> rows;
        juce::Viewport viewport;
        juce::TextButton addFilesButton, addFolderButton;
        bool dropHighlight = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileTreeView)
    };
}
