#include "BatchLog.h"

namespace rf::app
{
    bool BatchLog::open (const juce::File& folder, const juce::StringArray& headerLines)
    {
        file = juce::File();

        if (! folder.isDirectory() && folder.createDirectory().failed())
        {
            juce::Logger::writeToLog ("Batch log: cannot create " + folder.getFullPathName());
            return false;
        }

        const auto name = "Reamp Rig batch " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        const auto candidate = folder.getNonexistentChildFile (name, ".txt", false);

        if (! candidate.replaceWithText (headerLines.joinIntoString ("\n") + "\n\n", false, false, "\n"))
        {
            juce::Logger::writeToLog ("Batch log: cannot write " + candidate.getFullPathName());
            return false;
        }

        file = candidate;
        return true;
    }

    void BatchLog::append (const juce::StringArray& lines)
    {
        if (isOpen())
            file.appendText (lines.joinIntoString ("\n") + "\n", false, false, "\n");
    }

    void BatchLog::close (const juce::StringArray& footerLines)
    {
        if (isOpen())
            file.appendText ("\n" + footerLines.joinIntoString ("\n") + "\n", false, false, "\n");
    }
}
