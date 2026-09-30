#include "FileItem.h"

namespace rf::model
{
    juce::String toString (FileStatus status)
    {
        switch (status)
        {
            case FileStatus::queued:    return "Queued";
            case FileStatus::recording: return "Recording";
            case FileStatus::done:      return "Done";
            case FileStatus::skipped:   return "Skipped";
            case FileStatus::error:     return "Error";
        }

        return {};
    }
}
