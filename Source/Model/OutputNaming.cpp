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
    }

    juce::String validate (const NamingOptions& o)
    {
        if (hasSeparator (o.prefix) || hasSeparator (o.suffix))
            return "Prefix and suffix cannot contain / \\ or :";

        if (o.mode == DestinationMode::besideSource)
        {
            const auto name = o.subfolderName.trim();

            if (name.isEmpty())
                return "Enter a subfolder name in DESTINATION";

            if (hasSeparator (name) || name == "." || name == "..")
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
        auto stem = o.prefix + source.getFileNameWithoutExtension() + o.suffix;

        if (o.channelTag && tag.has_value())
            stem << (*tag == Channel::left ? "_L" : "_R");

        if (number > 1)
            stem << " (" << number << ")";

        return juce::File::createLegalFileName (stem + extension);
    }

    juce::File folder (const juce::File& source, const juce::File& root, const NamingOptions& o)
    {
        const auto sourceFolder = source.getParentDirectory();

        if (o.mode == DestinationMode::besideSource)
            return sourceFolder.getChildFile (juce::File::createLegalFileName (o.subfolderName.trim()));

        if (! o.mirrorStructure)
            return o.outputFolder;

        // The folder the user added (or the source's own folder for a single added file) is
        // mirrored together with everything below it.
        const auto top = (root != juce::File() && source.isAChildOf (root)) ? root : sourceFolder;
        const auto base = top.getParentDirectory();

        if (base == top)   // the file system root itself was added
            return o.outputFolder;

        return o.outputFolder.getChildFile (sourceFolder.getRelativePathFrom (base));
    }

    Target resolve (const juce::File& source, const juce::File& root, std::optional<Channel> tag, const NamingOptions& o)
    {
        const auto dir = folder (source, root, o);

        Target t;
        t.file = dir.getChildFile (fileName (source, o, tag));

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

                    if (! candidate.exists())
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
            return juce::File::createLegalFileName (o.subfolderName.trim()) + "/" + name;

        if (o.outputFolder == juce::File())
            return name;

        const auto dir = folder (source, root, o);
        const auto relative = dir == o.outputFolder ? juce::String() : dir.getRelativePathFrom (o.outputFolder) + "/";
        return o.outputFolder.getFileName() + "/" + relative + name;
    }
}
