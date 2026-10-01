#pragma once

#include <juce_core/juce_core.h>

/*  Text formatting for values shown in the UI (times, rates, bit depths). All output is
    meant for the mono font so columns line up.
*/
namespace rf::ui::format
{
    /** "mm:ss.mmm", or "h:mm:ss.mmm" from one hour. */
    inline juce::String time (double seconds)
    {
        const auto totalMs = (juce::int64) std::llround (juce::jmax (0.0, seconds) * 1000.0);
        const auto ms = (int) (totalMs % 1000);
        const auto s  = (int) ((totalMs / 1000) % 60);
        const auto m  = (int) ((totalMs / 60000) % 60);
        const auto h  = (int) (totalMs / 3600000);

        auto text = juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s).paddedLeft ('0', 2)
                  + "." + juce::String (ms).paddedLeft ('0', 3);

        return h > 0 ? juce::String (h) + ":" + text : text;
    }

    /** "44.1 kHz", "48 kHz", "22.05 kHz". */
    inline juce::String sampleRate (double hz)
    {
        if (hz <= 0.0)
            return "-";

        auto khz = juce::String (hz / 1000.0, 2);

        while (khz.containsChar ('.') && (khz.endsWithChar ('0') || khz.endsWithChar ('.')))
            khz = khz.dropLastCharacters (1);

        return khz + " kHz";
    }

    /** "16-bit", "24-bit", "32 float". */
    inline juce::String bitDepth (int bits, bool isFloat)
    {
        if (bits <= 0)
            return "-";

        return isFloat ? juce::String (bits) + " float" : juce::String (bits) + "-bit";
    }

    /** "Mono", "Stereo", "4 ch". */
    inline juce::String channels (int numChannels)
    {
        return numChannels == 1 ? juce::String ("Mono")
             : numChannels == 2 ? juce::String ("Stereo")
                                : juce::String (numChannels) + " ch";
    }

    /** "1 file", "12 files". */
    inline juce::String fileCount (int n)
    {
        return juce::String (n) + (n == 1 ? " file" : " files");
    }

    /** "25.7 ms", "6.42 ms" (two decimals below 10 ms). */
    inline juce::String milliseconds (double ms)
    {
        return juce::String (ms, std::abs (ms) < 10.0 ? 2 : 1) + " ms";
    }

    /** A round trip: "1234 smp · 25.7 ms". */
    inline juce::String latency (int samples, double ms)
    {
        return juce::String (samples) + " smp" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + milliseconds (ms);
    }

    /** "2026-10-01 14:02" in local time, or "unknown" for a zero time. */
    inline juce::String dateTime (const juce::Time& t)
    {
        return t.toMilliseconds() > 0 ? t.formatted ("%Y-%m-%d %H:%M") : juce::String ("unknown");
    }

    /** Replaces the user's home directory with "~". */
    inline juce::String displayPath (const juce::File& f)
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
        const auto path = f.getFullPathName();
        return path.startsWith (home + juce::File::getSeparatorString()) || path == home
                 ? "~" + path.substring (home.length())
                 : path;
    }
}
