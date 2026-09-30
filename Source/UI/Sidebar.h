#pragma once

#include "SidebarSections.h"

namespace rf::ui
{
    /** The 280 px right-hand sidebar: stacked sections in a vertically scrolling viewport. */
    class Sidebar final : public juce::Component
    {
    public:
        Sidebar();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Content final : public juce::Component
        {
        public:
            Content();

            int getPreferredHeight() const;
            void resized() override;

        private:
            AudioSection audio;
            SyncSection sync;
            DestinationSection destination;
            OptionsSection options;

            std::array<SidebarSection*, 4> sections { &audio, &sync, &destination, &options };
        };

        juce::Viewport viewport;
        Content content;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sidebar)
    };
}
