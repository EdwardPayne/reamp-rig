#pragma once

#include "FileItem.h"

#include <optional>
#include <set>

namespace rf::model
{
    /*  Where and under which name a processed file is written (PROMPT.md section 3.4).

        Name:   <prefix><source name without extension><suffix>[_L|_R].wav
                The channel tag is only added for sources with a channel choice (stereo) and
                when the option is on. Only characters no file system takes are removed
                (legalName: / \ : * ? " < > | and control characters; on Windows also trailing
                dots and spaces); # @ , ; and the rest stay. A name that would be longer than
                maxNameBytes loses the end of the source name only: prefix, suffix, tag, the
                auto-number and the extension are always kept.
        Folder: besideSource  -> <source folder>/<subfolder name>/
                singleFolder  -> <output folder>/                      (mirror off)
                                 <output folder>/<relative folders>/   (mirror on)
                The relative folders mirror the source's location below the parent of the
                folder (or file) the user added, so adding "Session A" gives
                "<output>/Session A/Takes/..." for "Session A/Takes/...". A volume root added
                as a whole mirrors everything below it.
        Collisions with an existing file: overwrite, skip, or auto-number ("name (2).wav").
        A name already written by the same batch (two sources that map to one output, e.g.
        "A/Riff.wav" and "B/Riff.wav" with mirroring off, or "x.wav" and "x.aif") is always
        auto-numbered, whatever the policy: one batch never overwrites its own output.

        Pure functions over juce::File; only resolve() looks at the file system (to see what
        exists). No GUI, message thread or any thread.
    */
    enum class DestinationMode { besideSource, singleFolder };
    enum class CollisionPolicy { autoNumber, overwrite, skip };

    struct NamingOptions
    {
        DestinationMode mode = DestinationMode::besideSource;
        juce::String subfolderName = "Reamped";
        juce::File outputFolder;                // singleFolder mode
        bool mirrorStructure = true;            // singleFolder mode
        juce::String prefix;
        juce::String suffix = "_reamp";
        bool channelTag = false;
        CollisionPolicy collision = CollisionPolicy::autoNumber;

        bool operator== (const NamingOptions&) const = default;
    };

    namespace OutputNaming
    {
        inline const juce::String extension = ".wav";

        /** Longest output file name, in UTF-8 bytes: the 255 of APFS, HFS+, NTFS and ext4
            minus the 15 bytes the writer's hidden temp name adds ("." and ".reamprig-part"). */
        constexpr int maxNameBytes = 240;

        /** `name` with the characters no file system accepts removed: / \ : * ? " < > | and
            control characters; on Windows trailing dots and spaces are trimmed too. */
        juce::String legalName (const juce::String& name);

        /** The DESTINATION subfolder name as it is used on disk (trimmed, legalName); empty when
            nothing usable is left. The folder scanner skips folders with exactly this name. */
        juce::String subfolderName (const NamingOptions&);

        /** Output files a batch has written so far (juce::File order: case-insensitive where
            the platform's file names are). */
        using WrittenSet = std::set<juce::File>;

        /** Plain-language problem with the options (empty when they are usable), e.g. no
            output folder chosen, or a subfolder name with a path separator in it. */
        juce::String validate (const NamingOptions&);

        /** "<prefix><name><suffix>[_L|_R][ (n)].wav". `tag` is the played channel when the
            source has a channel choice, nullopt for mono sources. `number` > 1 adds " (n)". */
        juce::String fileName (const juce::File& source, const NamingOptions&, std::optional<Channel> tag,
                               int number = 1);

        /** The folder the output goes to (not created). `root` is the folder or file the user
            added (FileItem::root); if empty, the source's own folder is used as the root. */
        juce::File folder (const juce::File& source, const juce::File& root, const NamingOptions&);

        struct Target
        {
            juce::File file;            // where to write (empty when skipped)
            bool skip = false;          // CollisionPolicy::skip and the file exists
            bool overwrites = false;    // CollisionPolicy::overwrite and the file exists
            juce::String reason;        // why it was skipped
            juce::String note;          // why the name was numbered although the policy is not
                                        // auto-number (another file of this batch has it), else empty
        };

        /** Applies the collision policy against what exists on disk right now, and treats a
            name in `writtenThisBatch` as taken whatever the policy (auto-numbered). */
        Target resolve (const juce::File& source, const juce::File& root, std::optional<Channel> tag,
                        const NamingOptions&, const WrittenSet& writtenThisBatch = {});

        /** The example line under the prefix/suffix fields: "Reamped/Riff 01_reamp.wav" or
            "<output folder name>/<mirrored folders>/Riff 01_reamp.wav". */
        juce::String example (const juce::File& source, const juce::File& root, std::optional<Channel> tag,
                              const NamingOptions&);
    }
}
