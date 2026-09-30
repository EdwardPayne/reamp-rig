#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rf::ui
{
    /*  The file list, grouped by folder.
        Phase 1: placeholder that shows the header and the empty-state drop hint only.
    */
    class FileTreeView final : public juce::Component
    {
    public:
        FileTreeView() = default;

        void paint (juce::Graphics&) override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileTreeView)
    };
}
