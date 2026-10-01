#pragma once

#include "FileItem.h"
#include "FolderScanner.h"

#include <set>
#include <vector>

namespace rf::model
{
    /*  The source file list, grouped by folder (PROMPT.md section 3.1).

        - Groups are keyed by each file's parent folder and appear in the order they were
          first added. Files inside a group are kept in natural file-name order, so adding the
          same files in a different order gives the same list.
        - A file (by absolute path) is only ever in the list once.
        - Holds the row selection and the "lead" item (the last clicked row, which the
          waveform panel shows), so selection rules can be unit tested.

        Message thread only. Listeners are called synchronously after every change.
    */
    class FileTree
    {
    public:
        struct Group
        {
            juce::File folder;
            std::vector<FileItem> files;
        };

        struct AddResult
        {
            int added = 0;
            int duplicates = 0;
            std::vector<ItemId> addedIds;
        };

        class Listener
        {
        public:
            virtual ~Listener() = default;
            virtual void fileTreeChanged() = 0;

            /** Only the progress of one item changed (the batch, about 10 times a second).
                Called instead of fileTreeChanged so views can repaint just that row. */
            virtual void fileProgressChanged (ItemId)   { fileTreeChanged(); }
        };

        FileTree() = default;

        //==============================================================================
        AddResult add (const std::vector<ScannedFile>&);
        void clear();

        const std::vector<Group>& getGroups() const noexcept   { return groups; }
        int getNumFiles() const noexcept;
        int countWithStatus (FileStatus) const noexcept;
        bool contains (const juce::File&) const;

        const FileItem* find (ItemId) const;
        const FileItem* findByFile (const juce::File&) const;

        /** Every item id in list order (group by group). */
        std::vector<ItemId> getAllIds() const;

        //==============================================================================
        // Selection
        bool isSelected (ItemId id) const              { return selection.count (id) > 0; }
        int getNumSelected() const noexcept            { return (int) selection.size(); }

        /** Selected ids in list order. */
        std::vector<ItemId> getSelectedIds() const;

        /** The item most recently clicked or selected; shown in the waveform panel. 0 if none. */
        ItemId getLead() const noexcept                { return lead; }

        void selectOnly (ItemId);
        void toggleSelected (ItemId);
        void setSelection (const std::vector<ItemId>& ids, ItemId newLead);
        void selectAll();
        void clearSelection();

        /** Removes `ids` from the selection (the lead stays the lead). Returns how many were
            selected. Used when a folder group is collapsed: hidden rows are never part of a
            bulk action (decision 2026-10-01). */
        int deselect (const std::vector<ItemId>& ids);

        //==============================================================================
        // Edits. Each returns the number of items actually changed.

        /** Sets the channel on the given items. Items without a channel choice (mono) are left
            untouched; that is the multi-select L/R rule from PROMPT.md section 3.1.6. */
        int setChannel (const std::vector<ItemId>& ids, Channel);
        int setChannelOfSelection (Channel c)          { return setChannel (getSelectedIds(), c); }

        int remove (const std::vector<ItemId>& ids);
        int removeSelected()                           { return remove (getSelectedIds()); }

        /** Back to Queued with zero progress; clears warnings, output file and note. */
        int resetStatus (const std::vector<ItemId>& ids);
        int resetStatusOfSelected()                    { return resetStatus (getSelectedIds()); }

        /** Items whose last take has any of the `mask` warnings (Done files only). */
        std::vector<ItemId> getIdsWithWarnings (juce::uint32 mask) const;

        /** Batch: sets the status and progress (keeps warnings, output and note). */
        bool setStatus (ItemId, FileStatus, double progress);

        /** Batch: the outcome of a take. */
        bool setResult (ItemId, FileStatus, double progress, juce::uint32 warnings,
                        const juce::File& outputFile, const juce::String& note);

        /** Batch: progress only; notifies Listener::fileProgressChanged. */
        bool setProgress (ItemId, double progress);

        //==============================================================================
        void addListener (Listener* l)                 { listeners.add (l); }
        void removeListener (Listener* l)              { listeners.remove (l); }

    private:
        FileItem* findMutable (ItemId);
        void changed();

        std::vector<Group> groups;
        std::set<juce::String> paths;   // dedupe keys of every file in the list
        std::set<ItemId> selection;
        ItemId lead = 0;
        ItemId nextId = 1;

        juce::ListenerList<Listener> listeners;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileTree)
    };
}
