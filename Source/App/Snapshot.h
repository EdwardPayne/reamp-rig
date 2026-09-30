#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::app
{
    /*  Development aid: renders a window, plus any JUCE popup/tooltip windows that overlap it,
        into a PNG using JUCE's own renderer. Used by `--snapshot=<file.png>` to verify the UI
        without needing macOS screen-recording permission. The native title bar is not included.
    */
    bool writeSnapshot (juce::Component& window, const juce::File& pngFile, float scale = 2.0f);
}
