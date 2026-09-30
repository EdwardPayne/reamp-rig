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

        //==============================================================================
        // Edits. Each returns the number of items actually changed.

        /** Sets the channel on the given items. Items without a channel choice (mono) are left
            untouched; that is the multi-select L/R rule from PROMPT.md section 3.1.6. */
        int setChannel (const std::vector<ItemId>& ids, Channel);
        int setChannelOfSelection (Channel c)          { return setChannel (getSelectedIds(), c); }

        int remove (const std::vector<ItemId>& ids);
        int removeSelected()                           { return remove (getSelectedIds()); }

        /** Back to Queued with zero progress. */
        int resetStatus (const std::vector<ItemId>& ids);
        int resetStatusOfSelected()                    { return resetStatus (getSelectedIds()); }

        /** Hook for the batch (phase 4). */
        bool setStatus (ItemId, FileStatus, double progress);

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
