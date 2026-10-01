#include "FileTree.h"

#include <algorithm>

namespace rf::model
{
    namespace
    {
        bool naturalLess (const FileItem& a, const FileItem& b)
        {
            return a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
        }
    }

    //==============================================================================
    FileTree::AddResult FileTree::add (const std::vector<ScannedFile>& scanned)
    {
        AddResult result;

        for (const auto& s : scanned)
        {
            const auto key = fileIdentity (s.file);

            if (! paths.insert (key).second)
            {
                ++result.duplicates;
                continue;
            }

            const auto folder = s.file.getParentDirectory();
            const auto folderKey = fileIdentity (folder);

            auto group = std::find_if (groups.begin(), groups.end(),
                                       [&] (const Group& g) { return g.key == folderKey; });

            if (group == groups.end())
            {
                groups.push_back ({ folder, {}, folderKey });
                group = std::prev (groups.end());
            }

            FileItem item;
            item.id = nextId++;
            keys[item.id] = key;
            item.file = s.file;
            item.info = s.info;
            item.root = s.root;

            auto& files = group->files;
            files.insert (std::upper_bound (files.begin(), files.end(), item, naturalLess), item);

            result.addedIds.push_back (item.id);
            ++result.added;
        }

        if (result.added > 0)
            changed();

        return result;
    }

    void FileTree::clear()
    {
        groups.clear();
        paths.clear();
        keys.clear();
        selection.clear();
        lead = 0;
        changed();
    }

    int FileTree::getNumFiles() const noexcept
    {
        int n = 0;

        for (const auto& g : groups)
            n += (int) g.files.size();

        return n;
    }

    int FileTree::countWithStatus (FileStatus status) const noexcept
    {
        int n = 0;

        for (const auto& g : groups)
            n += (int) std::count_if (g.files.begin(), g.files.end(),
                                      [status] (const FileItem& f) { return f.status == status; });

        return n;
    }

    bool FileTree::contains (const juce::File& f) const
    {
        return paths.count (fileIdentity (f)) > 0;
    }

    const FileItem* FileTree::find (ItemId id) const
    {
        return const_cast<FileTree*> (this)->findMutable (id);
    }

    const FileItem* FileTree::findByFile (const juce::File& f) const
    {
        const auto key = fileIdentity (f);

        for (const auto& g : groups)
            for (const auto& item : g.files)
                if (const auto k = keys.find (item.id); k != keys.end() && k->second == key)
                    return &item;

        return nullptr;
    }

    FileItem* FileTree::findMutable (ItemId id)
    {
        for (auto& g : groups)
            for (auto& item : g.files)
                if (item.id == id)
                    return &item;

        return nullptr;
    }

    std::vector<ItemId> FileTree::getAllIds() const
    {
        std::vector<ItemId> ids;

        for (const auto& g : groups)
            for (const auto& item : g.files)
                ids.push_back (item.id);

        return ids;
    }

    //==============================================================================
    std::vector<ItemId> FileTree::getSelectedIds() const
    {
        std::vector<ItemId> ids;

        for (const auto& g : groups)
            for (const auto& item : g.files)
                if (isSelected (item.id))
                    ids.push_back (item.id);

        return ids;
    }

    void FileTree::selectOnly (ItemId id)
    {
        setSelection ({ id }, id);
    }

    void FileTree::toggleSelected (ItemId id)
    {
        if (find (id) == nullptr)
            return;

        if (selection.erase (id) == 0)
            selection.insert (id);

        lead = id;
        changed();
    }

    void FileTree::setSelection (const std::vector<ItemId>& ids, ItemId newLead)
    {
        selection.clear();

        for (auto id : ids)
            if (find (id) != nullptr)
                selection.insert (id);

        lead = find (newLead) != nullptr ? newLead : 0;
        changed();
    }

    void FileTree::selectAll()
    {
        const auto ids = getAllIds();
        setSelection (ids, lead != 0 ? lead : (ids.empty() ? 0 : ids.front()));
    }

    void FileTree::clearSelection()
    {
        setSelection ({}, lead);
    }

    int FileTree::deselect (const std::vector<ItemId>& ids)
    {
        auto n = 0;

        for (auto id : ids)
            n += (int) selection.erase (id);

        if (n > 0)
            changed();

        return n;
    }

    //==============================================================================
    int FileTree::setChannel (const std::vector<ItemId>& ids, Channel channel)
    {
        int n = 0;

        for (auto id : ids)
        {
            if (id == channelLocked && id != 0)
                continue;

            if (auto* item = findMutable (id); item != nullptr && item->hasChannelChoice() && item->channel != channel)
            {
                item->channel = channel;
                ++n;
            }
        }

        if (n > 0)
            changed();

        return n;
    }

    int FileTree::remove (const std::vector<ItemId>& ids)
    {
        const std::set<ItemId> doomed (ids.begin(), ids.end());
        int n = 0;

        for (auto& g : groups)
        {
            const auto it = std::remove_if (g.files.begin(), g.files.end(), [&] (const FileItem& item)
            {
                if (doomed.count (item.id) == 0)
                    return false;

                if (const auto k = keys.find (item.id); k != keys.end())
                {
                    paths.erase (k->second);
                    keys.erase (k);
                }

                selection.erase (item.id);
                ++n;
                return true;
            });

            g.files.erase (it, g.files.end());
        }

        groups.erase (std::remove_if (groups.begin(), groups.end(), [] (const Group& g) { return g.files.empty(); }),
                      groups.end());

        if (doomed.count (lead) > 0)
            lead = 0;

        if (n > 0)
            changed();

        return n;
    }

    int FileTree::resetStatus (const std::vector<ItemId>& ids)
    {
        int n = 0;

        for (auto id : ids)
        {
            if (auto* item = findMutable (id); item != nullptr
                && (item->status != FileStatus::queued || item->progress != 0.0 || item->warnings != 0
                    || item->outputFile != juce::File() || item->note.isNotEmpty()))
            {
                item->status = FileStatus::queued;
                item->progress = 0.0;
                item->warnings = 0;
                item->outputFile = juce::File();
                item->note = {};
                ++n;
            }
        }

        if (n > 0)
            changed();

        return n;
    }

    std::vector<ItemId> FileTree::getIdsWithWarnings (juce::uint32 mask) const
    {
        std::vector<ItemId> ids;

        for (const auto& g : groups)
            for (const auto& item : g.files)
                if (item.status == FileStatus::done && (item.warnings & mask) != 0)
                    ids.push_back (item.id);

        return ids;
    }

    bool FileTree::setStatus (ItemId id, FileStatus status, double progress)
    {
        auto* item = findMutable (id);

        if (item == nullptr)
            return false;

        item->status = status;
        item->progress = juce::jlimit (0.0, 1.0, progress);
        changed();
        return true;
    }

    bool FileTree::setResult (ItemId id, FileStatus status, double progress, juce::uint32 warnings,
                              const juce::File& outputFile, const juce::String& note)
    {
        auto* item = findMutable (id);

        if (item == nullptr)
            return false;

        item->status = status;
        item->progress = juce::jlimit (0.0, 1.0, progress);
        item->warnings = warnings;
        item->outputFile = outputFile;
        item->note = note;
        changed();
        return true;
    }

    bool FileTree::setProgress (ItemId id, double progress)
    {
        auto* item = findMutable (id);

        if (item == nullptr)
            return false;

        progress = juce::jlimit (0.0, 1.0, progress);

        if (juce::exactlyEqual (item->progress, progress))
            return true;

        item->progress = progress;
        listeners.call ([id] (Listener& l) { l.fileProgressChanged (id); });
        return true;
    }

    void FileTree::changed()
    {
        listeners.call ([] (Listener& l) { l.fileTreeChanged(); });
    }
}
