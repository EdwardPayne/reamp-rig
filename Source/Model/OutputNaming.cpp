#include "OutputNaming.h"

namespace rf::model::OutputNaming
{
    namespace
    {
        constexpr int maxAutoNumber = 9999;

        bool hasSeparator (const juce::String& s)
        {
            return s.containsAnyOf ("/\\:");
        }

        int utf8Bytes (const juce::String& s)
        {
            return (int) s.getNumBytesAsUTF8();
        }

        /** Drops characters from the end until `s` fits `maxBytes` UTF-8 bytes (whole code
            points only, so a multi-byte character is never cut in half). */
        juce::String truncateToBytes (juce::String s, int maxBytes)
        {
            if (maxBytes <= 0)
                return {};

            while (s.isNotEmpty() && utf8Bytes (s) > maxBytes)
                s = s.dropLastCharacters (juce::jmax (1, (utf8Bytes (s) - maxBytes) / 4));

            return s;
        }
    }

    juce::String legalName (const juce::String& name)
    {
        juce::String result;
        result.preallocateBytes (name.getNumBytesAsUTF8());

        for (auto p = name.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();

            if (c < 0x20 || c == 0x7f || juce::String ("/\\:*?\"<>|").containsChar (c))
                continue;

            result += juce::String::charToString (c);
        }

       #if JUCE_WINDOWS
        // Windows drops trailing dots and spaces itself, so "Take." and "Take" would collide.
        while (result.endsWithChar ('.') || result.endsWithChar (' '))
            result = result.dropLastCharacters (1);
       #endif

        return result;
    }

    juce::String subfolderName (const NamingOptions& o)
    {
        return legalName (o.subfolderName.trim()).trim();
    }

    juce::String validate (const NamingOptions& o)
    {
        if (hasSeparator (o.prefix) || hasSeparator (o.suffix))
            return "Prefix and suffix cannot contain / \\ or :";

        if (o.mode == DestinationMode::besideSource)
        {
            const auto typed = o.subfolderName.trim();

            if (typed.isEmpty())
                return "Enter a subfolder name in DESTINATION";

            const auto name = subfolderName (o);

            if (hasSeparator (typed) || name.isEmpty() || name == "." || name == "..")
                return "The subfolder name must be a single folder name (no / \\ or :)";

            return {};
        }

        if (o.outputFolder == juce::File())
            return "Choose an output folder in DESTINATION";

        if (o.outputFolder.existsAsFile())
            return "The output folder is a file: " + o.outputFolder.getFullPathName();

        return {};
    }

    juce::String fileName (const juce::File& source, const NamingOptions& o, std::optional<Channel> tag, int number)
    {
        auto suffix = legalName (o.suffix);

        if (o.channelTag && tag.has_value())
            suffix << (*tag == Channel::left ? "_L" : "_R");

        const auto prefix = legalName (o.prefix);
        const auto tail = (number > 1 ? " (" + juce::String (number) + ")" : juce::String()) + extension;

        // Too long: only the source name is shortened, so the prefix, suffix, tag, number and
        // extension survive (and auto-numbering keeps producing different names).
        const auto budget = maxNameBytes - utf8Bytes (prefix + suffix + tail);
        const auto name = legalName (source.getFileNameWithoutExtension());

        if (budget >= 0)
            return prefix + truncateToBytes (name, budget) + suffix + tail;

        // An absurdly long prefix and suffix: they are shortened too, the number and extension never.
        return truncateToBytes (prefix + suffix, maxNameBytes - utf8Bytes (tail)) + tail;
    }

    juce::File folder (const juce::File& source, const juce::File& root, const NamingOptions& o)
    {
        const auto sourceFolder = source.getParentDirectory();

        if (o.mode == DestinationMode::besideSource)
            return sourceFolder.getChildFile (subfolderName (o));

        if (! o.mirrorStructure)
            return o.outputFolder;

        // The folder the user added (or the source's own folder for a single added file) is
        // mirrored together with everything below it.
        const auto top = (root != juce::File() && source.isAChildOf (root)) ? root : sourceFolder;
        const auto base = top.getParentDirectory();

        if (base == top)
        {
            // The file system (volume) root itself was added: it has no name of its own to
            // mirror, so the folders below it are mirrored ("/A/x.wav" -> "<out>/A/x.wav").
            return sourceFolder == top ? o.outputFolder : o.outputFolder.getChildFile (sourceFolder.getRelativePathFrom (top));
        }

        return o.outputFolder.getChildFile (sourceFolder.getRelativePathFrom (base));
    }

    Target resolve (const juce::File& source, const juce::File& root, std::optional<Channel> tag, const NamingOptions& o,
                    const WrittenSet& written)
    {
        const auto dir = folder (source, root, o);
        const auto writtenHere = [&written] (const juce::File& f) { return written.count (f) > 0; };

        Target t;
        t.file = dir.getChildFile (fileName (source, o, tag));

        if (writtenHere (t.file))
        {
            // Another source of this batch already went to this name. Overwriting or skipping
            // would lose one of the two takes, whatever the policy: number this one instead.
            const auto taken = t.file.getFileName();

            for (int n = 2; n <= maxAutoNumber; ++n)
            {
                const auto candidate = dir.getChildFile (fileName (source, o, tag, n));

                if (writtenHere (candidate))
                    continue;

                if (candidate.exists())
                {
                    if (o.collision != CollisionPolicy::overwrite)
                        continue;           // only overwrite may replace a file from before this batch

                    t.overwrites = true;
                }

                t.file = candidate;
                t.note = taken + " was already written by this batch; numbered " + candidate.getFileName();
                return t;
            }

            t.skip = true;
            t.reason = "no free name for " + taken;
            t.file = juce::File();
            return t;
        }

        if (! t.file.exists())
            return t;

        switch (o.collision)
        {
            case CollisionPolicy::overwrite:
                t.overwrites = true;
                return t;

            case CollisionPolicy::skip:
                t.skip = true;
                t.reason = t.file.getFileName() + " already exists";
                t.file = juce::File();
                return t;

            case CollisionPolicy::autoNumber:
                for (int n = 2; n <= maxAutoNumber; ++n)
                {
                    const auto candidate = dir.getChildFile (fileName (source, o, tag, n));

                    if (! candidate.exists() && ! writtenHere (candidate))
                    {
                        t.file = candidate;
                        return t;
                    }
                }

                t.skip = true;
                t.reason = "no free name for " + t.file.getFileName();
                t.file = juce::File();
                return t;
        }

        return t;
    }

    juce::String example (const juce::File& source, const juce::File& root, std::optional<Channel> tag,
                          const NamingOptions& o)
    {
        const auto name = fileName (source, o, tag);

        if (o.mode == DestinationMode::besideSource)
            return subfolderName (o) + "/" + name;

        if (o.outputFolder == juce::File())
            return name;

        const auto dir = folder (source, root, o);
        const auto relative = dir == o.outputFolder ? juce::String() : dir.getRelativePathFrom (o.outputFolder) + "/";
        return o.outputFolder.getFileName() + "/" + relative + name;
    }
}
