#include "FileTreeView.h"
#include "Fonts.h"
#include "Format.h"
#include "LookAndFeel.h"
#include "Theme.h"

#include <optional>
#include <set>

namespace rf::ui
{
    namespace colour = theme::colour;
    namespace metric = theme::metric;
    namespace type   = theme::type;

    using model::Channel;
    using model::FileItem;
    using model::FileStatus;
    using model::ItemId;

    namespace
    {
        constexpr int headerHeight       = 36;
        constexpr int columnHeaderHeight = 24;
        constexpr int rowHeight          = 28;
        constexpr int headerButtonHeight = 24;
        constexpr int accentEdgeWidth    = 2;
        constexpr int groupIndent        = 32;   // file names and group paths start here
        constexpr int lrBoxWidth         = 20;
        constexpr int lrBoxHeight        = 18;
        constexpr int progressHeight     = 4;
        constexpr int badgeSize          = 10;

        juce::String ellipsis()   { return utf8 ("\xe2\x80\xa6"); }

        //==============================================================================
        struct Columns
        {
            juce::Range<int> name, channels, duration, rate, depth, lr, status, progress;
        };

        Columns layOutColumns (int width)
        {
            Columns c;
            auto right = width - metric::sectionPadding;

            const auto take = [&right] (int w, int gapBefore)
            {
                const juce::Range<int> r (right - w, right);
                right -= w + gapBefore;
                return r;
            };

            c.progress = take (64, metric::sectionPadding);
            c.status   = take (152, metric::grid);
            c.lr       = take (2 * lrBoxWidth - 1 + 4, metric::sectionPadding);
            c.depth    = take (64, metric::grid);
            c.rate     = take (72, metric::grid);
            c.duration = take (80, metric::grid);
            c.channels = take (56, metric::sectionPadding);
            c.name     = { groupIndent, juce::jmax (groupIndent + 48, right + metric::grid) };
            return c;
        }

        juce::Rectangle<int> column (juce::Range<int> r, juce::Rectangle<int> row)
        {
            return { r.getStart(), row.getY(), r.getLength(), row.getHeight() };
        }

        /** Left and right halves of the L/R selector inside its column. */
        std::pair<juce::Rectangle<int>, juce::Rectangle<int>> lrBoxes (const Columns& c, juce::Rectangle<int> row)
        {
            const auto area = column (c.lr, row).withSizeKeepingCentre (2 * lrBoxWidth - 1, lrBoxHeight);
            return { area.withWidth (lrBoxWidth), area.withTrimmedLeft (lrBoxWidth - 1) };
        }

        /** Truncates from the start ("…/Session A/DI") so the end of a path stays visible. */
        juce::String fitFromStart (const juce::Font& font, const juce::String& text, int width)
        {
            if (juce::GlyphArrangement::getStringWidthInt (font, text) <= width)
                return text;

            for (int start = 1; start < text.length(); ++start)
            {
                const auto candidate = ellipsis() + text.substring (start);

                if (juce::GlyphArrangement::getStringWidthInt (font, candidate) <= width)
                    return candidate;
            }

            return ellipsis();
        }

        juce::Colour statusColour (FileStatus s)
        {
            switch (s)
            {
                case FileStatus::queued:    return colour::faint;
                case FileStatus::recording: return colour::accent;
                case FileStatus::done:      return colour::ok;
                case FileStatus::skipped:   return colour::muted;
                case FileStatus::error:     return colour::error;
            }

            return colour::faint;
        }

        juce::Font cellFont()          { return Fonts::mono (type::controlSize); }
        juce::Font badgeFont()         { return Fonts::mono (9.0f, FontWeight::bold, type::chipTracking); }
        juce::Font columnLabelFont()   { return Fonts::mono (10.0f, FontWeight::regular, 0.12f); }
        juce::Font statusFont()        { return Fonts::mono (10.0f, FontWeight::medium, type::chipTracking); }
    }

    //==============================================================================
    class FileTreeView::Rows final : public juce::Component,
                                     public juce::TooltipClient
    {
    public:
        explicit Rows (model::FileTree& t) : tree (t)
        {
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (true);
            rebuild();
        }

        //==============================================================================
        void rebuild()
        {
            rows.clear();
            const auto& groups = tree.getGroups();

            for (int g = 0; g < (int) groups.size(); ++g)
            {
                rows.push_back ({ Row::groupRow, g, 0 });

                if (! isCollapsed (groups[(size_t) g].folder))
                    for (const auto& item : groups[(size_t) g].files)
                        rows.push_back ({ Row::fileRow, g, item.id });
            }

            if (anchor != 0 && tree.find (anchor) == nullptr)
                anchor = 0;

            updateHover();
            repaint();
        }

        int getPreferredHeight() const   { return (int) rows.size() * rowHeight; }

        void setBatchState (ItemId newCurrent, bool active)
        {
            if (newCurrent == currentItem && active == batchActive)
                return;

            for (auto id : { currentItem, newCurrent })
                if (const auto index = indexOfItem (id); index >= 0)
                    repaint (rowBounds (index));

            currentItem = newCurrent;
            batchActive = active;
        }

        void repaintItem (ItemId id)
        {
            if (const auto index = indexOfItem (id); index >= 0)
                repaint (rowBounds (index));
        }

        std::function<void()> onSkipCurrent, onRedoWarnings;
        std::function<int()> getRedoableCount;

        /** Scrolls the enclosing viewport so the item's row is visible. */
        void scrollToItem (ItemId id)
        {
            auto* vp = findParentComponentOfClass<juce::Viewport>();
            const auto index = indexOfItem (id);

            if (vp == nullptr || index < 0)
                return;

            const auto top = index * rowHeight;
            const auto viewTop = vp->getViewPositionY();
            const auto viewHeight = vp->getViewHeight();

            if (top < viewTop)
                vp->setViewPosition (0, top);
            else if (top + rowHeight > viewTop + viewHeight)
                vp->setViewPosition (0, top + rowHeight - viewHeight);
        }

        //==============================================================================
        void paint (juce::Graphics& g) override
        {
            g.fillAll (colour::panel);

            const auto columns = layOutColumns (getWidth());
            const auto clip = g.getClipBounds();
            const auto first = juce::jmax (0, clip.getY() / rowHeight);
            const auto last  = juce::jmin ((int) rows.size() - 1, clip.getBottom() / rowHeight);

            for (int i = first; i <= last; ++i)
            {
                const juce::Rectangle<int> area (0, i * rowHeight, getWidth(), rowHeight);
                const auto& row = rows[(size_t) i];

                if (row.kind == Row::groupRow)
                    paintGroup (g, area, tree.getGroups()[(size_t) row.group], columns, i == hoveredRow);
                else if (const auto* item = tree.find (row.id))
                    paintFile (g, area, *item, columns, i == hoveredRow);
            }
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override    { setHover (e.getPosition()); }
        void mouseEnter (const juce::MouseEvent& e) override   { setHover (e.getPosition()); }
        void mouseExit (const juce::MouseEvent&) override      { setHover ({ -1, -1 }); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto index = rowAt (e.y);

            if (index < 0)
            {
                if (! e.mods.isAnyModifierKeyDown())
                    tree.clearSelection();

                return;
            }

            const auto row = rows[(size_t) index];

            if (row.kind == Row::groupRow)
            {
                if (! e.mods.isPopupMenu())
                    toggleCollapsed (tree.getGroups()[(size_t) row.group].folder);

                return;
            }

            const auto id = row.id;

            if (e.mods.isPopupMenu())
            {
                if (! tree.isSelected (id))
                    selectOnly (id);

                showContextMenu();
                return;
            }

            if (const auto channel = lrHit (index, e.getPosition()))
            {
                if (tree.isSelected (id))
                {
                    tree.setSelection (tree.getSelectedIds(), id);
                    tree.setChannelOfSelection (*channel);
                }
                else
                {
                    selectOnly (id);
                    tree.setChannel ({ id }, *channel);
                }

                return;
            }

            if (e.mods.isShiftDown() && anchor != 0)
            {
                auto ids = visibleRange (anchor, id);

                if (e.mods.isCommandDown())
                    for (auto existing : tree.getSelectedIds())
                        ids.push_back (existing);

                tree.setSelection (ids, id);
            }
            else if (e.mods.isCommandDown())
            {
                anchor = id;
                tree.toggleSelected (id);
            }
            else
            {
                selectOnly (id);
            }
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            const auto mods = key.getModifiers();
            const auto code = key.getKeyCode();
            const auto upper = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

            if (mods.isCommandDown() && upper == 'A')
            {
                // Every visible file: files in collapsed groups stay out of bulk actions.
                const auto ids = visibleFileIds();
                const auto lead = tree.getLead();
                tree.setSelection (ids, std::find (ids.begin(), ids.end(), lead) != ids.end() ? lead
                                                                                                : (ids.empty() ? 0 : ids.front()));
                return true;
            }

            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            {
                removeSelected();
                return true;
            }

            if (! mods.isCommandDown() && ! mods.isAltDown() && ! mods.isCtrlDown() && (upper == 'L' || upper == 'R'))
            {
                tree.setChannelOfSelection (upper == 'L' ? Channel::left : Channel::right);
                return true;
            }

            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            {
                moveLead (code == juce::KeyPress::upKey ? -1 : 1, mods.isShiftDown());
                return true;
            }

            return false;
        }

        //==============================================================================
        juce::String getTooltip() override
        {
            const auto pos = getMouseXYRelative();
            const auto index = rowAt (pos.y);

            if (index < 0)
                return {};

            const auto& row = rows[(size_t) index];

            if (row.kind == Row::groupRow)
                return tree.getGroups()[(size_t) row.group].folder.getFullPathName()
                       + "\nClick to collapse or expand this folder (collapsing deselects its files).";

            const auto* item = tree.find (row.id);

            if (item == nullptr)
                return {};

            const auto columns = layOutColumns (getWidth());

            if (column (columns.lr, rowBounds (index)).contains (pos))
                return item->hasChannelChoice()
                         ? "Channel sent to the amp. Applies to all selected stereo files (L / R)."
                         : "Mono file: played as is, no channel choice.";

            if (column (columns.status, rowBounds (index)).contains (pos)
                || column (columns.progress, rowBounds (index)).contains (pos))
            {
                auto tip = model::toString (item->status);

                if (item->note.isNotEmpty())
                    tip << ": " << item->note;

                if (item->outputFile != juce::File())
                    tip << "\nWritten to " << item->outputFile.getFullPathName();

                if (item->warnings != 0)
                    tip << "\nWarnings: " << model::Warning::describeAll (item->warnings, "; ");

                return tip;
            }

            return item->file.getFullPathName()
                   + "\nShift-click: range. Cmd-click: add or remove. Right-click: actions."
                     "\nKeys: Space audition, L / R channel, Delete remove, Cmd-A select all, Up / Down move.";
        }

    private:
        struct Row
        {
            enum Kind { groupRow, fileRow };
            Kind kind;
            int group;
            ItemId id;
        };

        //==============================================================================
        void paintGroup (juce::Graphics& g, juce::Rectangle<int> area, const model::FileTree::Group& group,
                         const Columns& columns, bool hovered) const
        {
            g.setColour (hovered ? colour::panel2 : colour::bg);
            g.fillRect (area);

            g.setColour (colour::lineSoft);
            g.fillRect (area.withHeight (1));
            g.fillRect (area.withTop (area.getBottom() - 1));

            const juce::Point<float> chevronCentre ((float) metric::sectionPadding + 4.0f, (float) area.getCentreY());

            if (isCollapsed (group.folder))
                drawRightChevron (g, chevronCentre, 4.0f, colour::faint);
            else
                drawChevron (g, chevronCentre, 4.0f, true, colour::faint);

            const auto countText = format::fileCount ((int) group.files.size());
            const auto font = cellFont();
            const auto countWidth = juce::GlyphArrangement::getStringWidthInt (font, countText);
            const auto countArea = juce::Rectangle<int> (columns.progress.getEnd() - countWidth, area.getY(),
                                                         countWidth, area.getHeight());

            g.setFont (font);
            g.setColour (colour::faint);
            g.drawText (countText, countArea, juce::Justification::centredRight, false);

            const auto pathArea = area.withLeft (groupIndent).withRight (countArea.getX() - metric::sectionPadding);
            g.setColour (colour::heading);
            g.setFont (Fonts::mono (type::controlSize, FontWeight::medium));
            g.drawText (fitFromStart (g.getCurrentFont(), format::displayPath (group.folder), pathArea.getWidth()),
                        pathArea, juce::Justification::centredLeft, false);
        }

        void paintFile (juce::Graphics& g, juce::Rectangle<int> area, const FileItem& item,
                        const Columns& columns, bool hovered) const
        {
            const auto selected = tree.isSelected (item.id);
            const auto isCurrent = item.id == currentItem;

            if (selected || hovered)
            {
                g.setColour (colour::panel2);
                g.fillRect (area);
            }

            if (isCurrent)
            {
                // The file being recorded: accent tint over the row, distinct from selection.
                g.setColour (colour::accent.withAlpha (0.12f));
                g.fillRect (area);
            }

            if (selected || isCurrent)
            {
                g.setColour (colour::accent);
                g.fillRect (area.withWidth (accentEdgeWidth));
            }

            g.setColour (colour::lineSoft);
            g.fillRect (area.withTop (area.getBottom() - 1).withTrimmedLeft (groupIndent));

            g.setFont (cellFont());

            g.setColour (selected || isCurrent ? colour::heading : colour::text);
            g.drawText (item.file.getFileName(), column (columns.name, area).withTrimmedRight (metric::grid),
                        juce::Justification::centredLeft, true);

            const auto& info = item.info;
            g.setColour (colour::faint);
            g.drawText (format::channels (info.numChannels), column (columns.channels, area), juce::Justification::centredLeft, true);
            g.drawText (format::time (info.getDurationSeconds()), column (columns.duration, area), juce::Justification::centredLeft, true);
            g.drawText (format::sampleRate (info.sampleRate), column (columns.rate, area), juce::Justification::centredLeft, true);
            g.drawText (format::bitDepth (info.bitsPerSample, info.isFloatingPoint), column (columns.depth, area),
                        juce::Justification::centredLeft, true);

            paintChannelSelector (g, item, lrBoxes (columns, area), hoveredChannel (item.id));
            paintStatus (g, item, column (columns.status, area));
            paintProgress (g, item, column (columns.progress, area));
        }

        void paintChannelSelector (juce::Graphics& g, const FileItem& item,
                                   std::pair<juce::Rectangle<int>, juce::Rectangle<int>> boxes,
                                   std::optional<Channel> hovered) const
        {
            const auto enabled = item.hasChannelChoice();
            g.setFont (Fonts::mono (type::fieldLabelSize, FontWeight::medium));

            for (const auto channel : { Channel::left, Channel::right })
            {
                const auto box = channel == Channel::left ? boxes.first : boxes.second;
                const auto label = channel == Channel::left ? "L" : "R";

                if (! enabled)
                {
                    g.setColour (colour::lineSoft);
                    g.drawRect (box, 1);
                    g.setColour (colour::muted.withAlpha (0.6f));
                    g.drawText (label, box, juce::Justification::centred, false);
                    continue;
                }

                if (item.channel == channel)
                {
                    g.setColour (colour::accent);
                    g.fillRect (box);
                    g.setColour (colour::bg);
                }
                else
                {
                    const auto hot = hovered == channel;

                    if (hot)
                    {
                        g.setColour (colour::panel2);
                        g.fillRect (box);
                    }

                    g.setColour (hot ? colour::lineStrong : colour::line);
                    g.drawRect (box, 1);
                    g.setColour (hot ? colour::heading : colour::faint);
                }

                g.drawText (label, box, juce::Justification::centred, false);
            }
        }

        static void paintStatus (juce::Graphics& g, const FileItem& item, juce::Rectangle<int> area)
        {
            const auto status = item.status;
            const auto c = statusColour (status);

            if (status == FileStatus::error)
            {
                drawBadge (g, area.removeFromLeft (badgeSize).withSizeKeepingCentre (badgeSize, badgeSize).toFloat(), "!", c);
                area.removeFromLeft (6);
            }

            const auto text = model::toString (status).toUpperCase();
            const auto font = statusFont();
            g.setColour (c);
            g.setFont (font);
            g.drawText (text, area, juce::Justification::centredLeft, true);

            if (item.warnings == 0)
                return;

            // Warning badges after the status: outlined warn-coloured tags, "+N" if they do
            // not all fit. The tooltip spells them out.
            area.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (font, text) + 6);

            const auto tagFont = badgeFont();
            int remaining = 0;

            for (auto flag : model::Warning::all)
                if ((item.warnings & flag) != 0)
                    ++remaining;

            g.setFont (tagFont);

            for (auto flag : model::Warning::all)
            {
                if ((item.warnings & flag) == 0)
                    continue;

                const auto code = model::Warning::code (flag);
                const auto width = juce::GlyphArrangement::getStringWidthInt (tagFont, code) + 6;
                const auto more = "+" + juce::String (remaining);
                const auto moreWidth = juce::GlyphArrangement::getStringWidthInt (tagFont, more);

                if (width + (remaining > 1 ? moreWidth + 4 : 0) > area.getWidth())
                {
                    g.setColour (colour::warn);
                    g.drawText (more, area, juce::Justification::centredLeft, false);
                    return;
                }

                const auto tag = area.removeFromLeft (width).withSizeKeepingCentre (width, 14);
                g.setColour (colour::warn);
                g.drawRect (tag, 1);
                g.drawText (code, tag, juce::Justification::centred, false);
                area.removeFromLeft (4);
                --remaining;
            }
        }

        static void paintProgress (juce::Graphics& g, const FileItem& item, juce::Rectangle<int> area)
        {
            const auto bar = area.withSizeKeepingCentre (area.getWidth(), progressHeight);
            g.setColour (colour::lineSoft);
            g.fillRect (bar);

            const auto progress = item.status == FileStatus::done ? 1.0 : item.progress;
            g.setColour (item.status == FileStatus::done ? colour::ok : colour::accent);
            g.fillRect (bar.withWidth (juce::roundToInt ((double) bar.getWidth() * juce::jlimit (0.0, 1.0, progress))));
        }

        //==============================================================================
        juce::Rectangle<int> rowBounds (int index) const   { return { 0, index * rowHeight, getWidth(), rowHeight }; }

        int rowAt (int y) const
        {
            const auto index = y / rowHeight;
            return y >= 0 && index < (int) rows.size() ? index : -1;
        }

        int indexOfItem (ItemId id) const
        {
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(size_t) i].kind == Row::fileRow && rows[(size_t) i].id == id)
                    return i;

            return -1;
        }

        std::optional<Channel> lrHit (int index, juce::Point<int> pos) const
        {
            const auto* item = tree.find (rows[(size_t) index].id);

            if (item == nullptr || ! item->hasChannelChoice())
                return std::nullopt;

            const auto boxes = lrBoxes (layOutColumns (getWidth()), rowBounds (index));

            if (boxes.first.contains (pos))   return Channel::left;
            if (boxes.second.contains (pos))  return Channel::right;
            return std::nullopt;
        }

        std::optional<Channel> hoveredChannel (ItemId id) const
        {
            if (hoveredRow < 0 || rows[(size_t) hoveredRow].id != id)
                return std::nullopt;

            return lrHit (hoveredRow, hoverPos);
        }

        std::vector<ItemId> visibleFileIds() const
        {
            std::vector<ItemId> ids;

            for (const auto& r : rows)
                if (r.kind == Row::fileRow)
                    ids.push_back (r.id);

            return ids;
        }

        /** Visible file ids from `a` to `b` inclusive, in list order. */
        std::vector<ItemId> visibleRange (ItemId a, ItemId b) const
        {
            const auto ids = visibleFileIds();
            const auto ia = std::find (ids.begin(), ids.end(), a);
            const auto ib = std::find (ids.begin(), ids.end(), b);

            if (ia == ids.end() || ib == ids.end())
                return { b };

            return ia <= ib ? std::vector<ItemId> (ia, ib + 1) : std::vector<ItemId> (ib, ia + 1);
        }

        //==============================================================================
        void selectOnly (ItemId id)
        {
            anchor = id;
            tree.selectOnly (id);
        }

        void moveLead (int delta, bool extend)
        {
            const auto ids = visibleFileIds();

            if (ids.empty())
                return;

            const auto current = std::find (ids.begin(), ids.end(), tree.getLead());
            const auto index = current == ids.end() ? (delta > 0 ? 0 : (int) ids.size() - 1)
                                                    : juce::jlimit (0, (int) ids.size() - 1,
                                                                    (int) (current - ids.begin()) + delta);
            const auto id = ids[(size_t) index];

            if (extend && anchor != 0)
                tree.setSelection (visibleRange (anchor, id), id);
            else
                selectOnly (id);

            scrollToItem (id);
        }

        void removeSelected()
        {
            // Keep a sensible lead after removal: the next visible file below the selection.
            const auto ids = visibleFileIds();
            ItemId next = 0;

            for (auto it = ids.rbegin(); it != ids.rend(); ++it)
            {
                if (tree.isSelected (*it))
                    break;

                next = *it;
            }

            if (tree.removeSelected() > 0 && next != 0)
                selectOnly (next);
        }

        /** Reset status of the selection, except the file the batch is recording. */
        void resetSelected()
        {
            auto ids = tree.getSelectedIds();
            ids.erase (std::remove (ids.begin(), ids.end(), currentItem), ids.end());
            tree.resetStatus (ids);
        }

        void showContextMenu()
        {
            const auto selected = tree.getSelectedIds();
            const auto anyStereo = std::any_of (selected.begin(), selected.end(), [this] (ItemId id)
            {
                const auto* item = tree.find (id);
                return item != nullptr && item->hasChannelChoice();
            });

            const auto count = (int) selected.size();
            const auto suffix = count > 1 ? " (" + juce::String (count) + ")" : juce::String();

            juce::PopupMenu menu;
            menu.addSectionHeader (count > 1 ? format::fileCount (count) + " selected" : juce::String ("File"));

            juce::PopupMenu::Item left ("Use left channel");
            left.setID (1).setEnabled (anyStereo);
            left.shortcutKeyDescription = "L";
            menu.addItem (left);

            juce::PopupMenu::Item right ("Use right channel");
            right.setID (2).setEnabled (anyStereo);
            right.shortcutKeyDescription = "R";
            menu.addItem (right);

            menu.addSeparator();
            menu.addItem (3, "Reset status" + suffix);

            const auto withWarnings = getRedoableCount != nullptr ? getRedoableCount()
                                                                  : (int) tree.getIdsWithWarnings (model::Warning::redoable).size();
            menu.addItem (5, "Redo files with warnings" + (withWarnings > 0 ? " (" + juce::String (withWarnings) + ")" : juce::String()),
                          withWarnings > 0 && ! batchActive);
            menu.addItem (6, "Skip current file", batchActive && currentItem != 0);

            menu.addSeparator();
            juce::PopupMenu::Item remove ("Remove" + suffix);
            remove.setID (4);
            remove.shortcutKeyDescription = utf8 ("\xe2\x8c\xab"); // ⌫
            menu.addItem (remove);

            juce::Component::SafePointer<Rows> safeThis (this);

            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safeThis] (int result)
            {
                if (safeThis == nullptr)
                    return;

                auto& t = safeThis->tree;

                switch (result)
                {
                    case 1: t.setChannelOfSelection (Channel::left);  break;
                    case 2: t.setChannelOfSelection (Channel::right); break;
                    case 3: safeThis->resetSelected();                break;
                    case 4: safeThis->removeSelected();               break;
                    case 5: if (safeThis->onRedoWarnings != nullptr) safeThis->onRedoWarnings(); break;
                    case 6: if (safeThis->onSkipCurrent != nullptr)  safeThis->onSkipCurrent();  break;
                    default: break;
                }
            });
        }

        //==============================================================================
        bool isCollapsed (const juce::File& folder) const
        {
            return collapsed.count (folder.getFullPathName()) > 0;
        }

        void toggleCollapsed (const juce::File& folder)
        {
            const auto key = folder.getFullPathName();

            if (collapsed.erase (key) == 0)
            {
                collapsed.insert (key);

                // Hidden rows are never part of a bulk action (decision 2026-10-01).
                std::vector<ItemId> hidden;

                for (const auto& g : tree.getGroups())
                    if (g.folder == folder)
                        for (const auto& item : g.files)
                            hidden.push_back (item.id);

                tree.deselect (hidden);   // notifies the tree, which rebuilds the rows
            }

            rebuild();

            if (auto* view = findParentComponentOfClass<FileTreeView>())
                view->updateRowsSize();
        }

        void setHover (juce::Point<int> pos)
        {
            const auto index = pos.x >= 0 ? rowAt (pos.y) : -1;

            if (index != hoveredRow || pos != hoverPos)
            {
                if (hoveredRow >= 0)  repaint (rowBounds (hoveredRow));
                hoveredRow = index;
                hoverPos = pos;
                if (hoveredRow >= 0)  repaint (rowBounds (hoveredRow));
            }
        }

        void updateHover()
        {
            hoveredRow = -1;

            if (isMouseOver (false))
                setHover (getMouseXYRelative());
        }

        //==============================================================================
        model::FileTree& tree;
        std::vector<Row> rows;
        std::set<juce::String> collapsed;
        ItemId anchor = 0;
        int hoveredRow = -1;
        juce::Point<int> hoverPos { -1, -1 };
        ItemId currentItem = 0;
        bool batchActive = false;
    };

    //==============================================================================
    FileTreeView::FileTreeView (model::FileTree& t)
        : tree (t),
          rows (std::make_unique<Rows> (t)),
          addFilesButton ("Add files" + ellipsis()),
          addFolderButton ("Add folder" + ellipsis())
    {
        viewport.setViewedComponent (rows.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (metric::grid);
        addChildComponent (viewport);

        for (auto* b : { &addFilesButton, &addFolderButton })
        {
            setButtonStyle (*b, ButtonStyle::secondary);
            addAndMakeVisible (b);
        }

        addFilesButton.setTooltip ("Choose audio files to add to the list (or drop them on the window).");
        addFolderButton.setTooltip ("Choose a folder to scan for audio files, with its subfolders when Include subfolders "
                                    "is on (OPTIONS). Output folders (\"Reamped\") are skipped.");
        addFilesButton.onClick  = [this] { if (onAddFiles != nullptr)  onAddFiles(); };
        addFolderButton.onClick = [this] { if (onAddFolder != nullptr) onAddFolder(); };

        setButtonStyle (redoButton, ButtonStyle::secondary);
        redoButton.setTooltip ("Queue again every file whose take had a dropout (XR), silence (SIL) or clipping (CLIP), and "
                               "every NC file whose device configuration has been synced since.");
        redoButton.onClick = [this] { if (onRedoWarnings != nullptr) onRedoWarnings(); };
        addChildComponent (redoButton);

        rows->onSkipCurrent  = [this] { if (onSkipCurrent != nullptr)  onSkipCurrent(); };
        rows->getRedoableCount = [this] { return countRedoable(); };
        rows->onRedoWarnings = [this] { if (onRedoWarnings != nullptr) onRedoWarnings(); };

        tree.addListener (this);
        fileTreeChanged();
    }

    FileTreeView::~FileTreeView()
    {
        tree.removeListener (this);
    }

    void FileTreeView::setDropHighlight (bool shouldHighlight)
    {
        if (dropHighlight != shouldHighlight)
        {
            dropHighlight = shouldHighlight;
            repaint();
        }
    }

    void FileTreeView::focusList()
    {
        if (rows->isShowing())
            rows->grabKeyboardFocus();
    }

    juce::Component& FileTreeView::getListComponent() noexcept
    {
        return *rows;
    }

    void FileTreeView::setBatchState (model::ItemId current, bool active)
    {
        rows->setBatchState (current, active);
        batchActive = active;
        updateRedoButton();
    }

    void FileTreeView::fileProgressChanged (model::ItemId id)
    {
        rows->repaintItem (id);
    }

    int FileTreeView::countRedoable() const
    {
        return getRedoableIds != nullptr ? (int) getRedoableIds().size()
                                         : (int) tree.getIdsWithWarnings (model::Warning::redoable).size();
    }

    juce::String FileTreeView::getTooltip()
    {
        // Only the empty drop zone; the rows, buttons and scrollbar have their own.
        if (tree.getNumFiles() == 0 && getMouseXYRelative().y > headerHeight)
            return "Drop DI files or folders anywhere on the window, or use Add files" + ellipsis() + " / Add folder"
                 + ellipsis() + ". Folders are scanned with their subfolders when Include subfolders (OPTIONS) is on.";

        return {};
    }

    void FileTreeView::updateRedoButton()
    {
        const auto show = ! batchActive && countRedoable() > 0;

        if (show != redoButton.isVisible())
        {
            redoButton.setVisible (show);
            resized();
            repaint (0, 0, getWidth(), headerHeight);
        }
    }

    void FileTreeView::fileTreeChanged()
    {
        updateRedoButton();
        rows->rebuild();
        viewport.setVisible (tree.getNumFiles() > 0);
        updateRowsSize();

        // Scroll to the lead only when it changes, so batch status updates do not pull the
        // list back while the user looks elsewhere.
        if (tree.getLead() != scrolledLead && tree.getLead() != 0 && tree.isSelected (tree.getLead()))
            rows->scrollToItem (tree.getLead());

        scrolledLead = tree.getLead();

        repaint (0, 0, getWidth(), headerHeight);

        if (tree.getNumFiles() == 0)
            repaint();
    }

    void FileTreeView::updateRowsSize()
    {
        // At least as tall as the viewport, so a click below the last row clears the selection.
        const auto height = rows->getPreferredHeight();
        const auto needsScroll = height > viewport.getHeight();
        rows->setSize (viewport.getWidth() - (needsScroll ? viewport.getScrollBarThickness() : 0),
                       juce::jmax (height, viewport.getHeight()));
    }

    juce::String FileTreeView::getCountText() const
    {
        const auto numSelected = tree.getNumSelected();
        auto text = format::fileCount (tree.getNumFiles());

        if (numSelected > 0)
            text << utf8 (" \xc2\xb7 ") << numSelected << " selected";

        return text;
    }

    void FileTreeView::paint (juce::Graphics& g)
    {
        g.fillAll (colour::panel);

        auto area = getLocalBounds();

        // Header strip: label, count, buttons.
        auto header = area.removeFromTop (headerHeight);
        g.setColour (colour::line);
        g.fillRect (header.removeFromBottom (1));

        header.reduce (metric::sectionPadding, 0);
        drawSectionLabel (g, "Files", header);

        header.setRight ((redoButton.isVisible() ? redoButton.getX() : addFilesButton.getX()) - metric::sectionPadding);
        g.setColour (colour::muted);
        g.setFont (Fonts::mono (type::fieldLabelSize));
        g.drawText (getCountText(), header, juce::Justification::centredRight, false);

        if (tree.getNumFiles() > 0)
        {
            // Column header.
            auto strip = area.removeFromTop (columnHeaderHeight);
            g.setColour (colour::lineSoft);
            g.fillRect (strip.removeFromBottom (1));

            const auto columns = layOutColumns (rows->getWidth());
            const std::pair<juce::Range<int>, const char*> labels[] =
            {
                { columns.name, "Name" }, { columns.channels, "Ch" }, { columns.duration, "Length" },
                { columns.rate, "Rate" }, { columns.depth, "Depth" }, { columns.lr, "L/R" },
                { columns.status, "Status" }, { columns.progress, "Progress" }
            };

            g.setFont (columnLabelFont());
            g.setColour (colour::muted);

            for (const auto& [range, text] : labels)
                g.drawText (juce::String (text).toUpperCase(), column (range, strip),
                            range == columns.lr ? juce::Justification::centred : juce::Justification::centredLeft, false);

            return;
        }

        // Empty-state drop zone.
        const auto dropZone = area.reduced (metric::sectionPadding);
        g.setColour (dropHighlight ? colour::accent : colour::line);
        g.drawRect (dropZone.toFloat(), metric::borderWidth);

        auto text = dropZone.withSizeKeepingCentre (dropZone.getWidth(), 48);

        g.setColour (colour::heading);
        g.setFont (Fonts::mono (type::bodySize, FontWeight::medium));
        g.drawText ("Drop DI files or folders here", text.removeFromTop (24),
                    juce::Justification::centred, true);

        g.setColour (colour::faint);
        g.setFont (Fonts::sans (type::helpSize));
        g.drawText ("WAV, AIFF or FLAC. Folders are scanned and grouped by location.",
                    text, juce::Justification::centred, true);
    }

    void FileTreeView::paintOverChildren (juce::Graphics& g)
    {
        if (dropHighlight && tree.getNumFiles() > 0)
        {
            g.setColour (colour::accent);
            g.drawRect (getLocalBounds().withTrimmedTop (headerHeight), 1);
        }
    }

    void FileTreeView::resized()
    {
        auto header = getLocalBounds().removeFromTop (headerHeight).withTrimmedBottom (1)
                                      .reduced (metric::sectionPadding, 0);

        const auto buttonWidth = [] (juce::TextButton& b)
        {
            return juce::GlyphArrangement::getStringWidthInt (Fonts::mono (type::buttonSize, FontWeight::medium,
                                                                           type::buttonTracking),
                                                              b.getButtonText().toUpperCase())
                   + 3 * metric::grid;
        };

        addFolderButton.setBounds (header.removeFromRight (buttonWidth (addFolderButton))
                                         .withSizeKeepingCentre (buttonWidth (addFolderButton), headerButtonHeight));
        header.removeFromRight (metric::grid);
        addFilesButton.setBounds (header.removeFromRight (buttonWidth (addFilesButton))
                                        .withSizeKeepingCentre (buttonWidth (addFilesButton), headerButtonHeight));

        if (redoButton.isVisible())
        {
            header.removeFromRight (metric::grid);
            redoButton.setBounds (header.removeFromRight (buttonWidth (redoButton))
                                        .withSizeKeepingCentre (buttonWidth (redoButton), headerButtonHeight));
        }

        viewport.setBounds (getLocalBounds().withTrimmedTop (headerHeight + columnHeaderHeight));
        updateRowsSize();
    }
}
