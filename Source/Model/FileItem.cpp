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

    namespace Warning
    {
        juce::String code (juce::uint32 flag)
        {
            switch (flag)
            {
                case resampled:     return "RS";
                case notCalibrated: return "NC";
                case dropout:       return "XR";
                case silence:       return "SIL";
                case clipped:       return "CLIP";
                default:            return {};
            }
        }

        juce::String describe (juce::uint32 flag)
        {
            switch (flag)
            {
                case resampled:     return "resampled (the device could not run at the file's sample rate)";
                case notCalibrated: return "not calibrated (latency estimated from the driver, not measured)";
                case dropout:       return "dropout during the take (xrun, callback gap or record buffer overflow)";
                case silence:       return "recorded silence? (peak below -60 dBFS)";
                case clipped:       return "clipped (the recording reached full scale)";
                default:            return {};
            }
        }

        juce::String describeAll (juce::uint32 flags, const juce::String& separator)
        {
            juce::StringArray parts;

            for (auto flag : all)
                if ((flags & flag) != 0)
                    parts.add (describe (flag));

            return parts.joinIntoString (separator);
        }
    }
}
