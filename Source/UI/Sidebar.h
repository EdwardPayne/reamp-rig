#pragma once

#include "SidebarSections.h"

namespace rf::ui
{
    /** The 280 px right-hand sidebar: stacked sections in a vertically scrolling viewport. */
    class Sidebar final : public juce::Component
    {
    public:
        Sidebar();

        AudioSection& getAudioSection() noexcept       { return content.audio; }
        SyncSection& getSyncSection() noexcept         { return content.sync; }
        DestinationSection& getDestinationSection() noexcept   { return content.destination; }
        OptionsSection& getOptionsSection() noexcept   { return content.options; }

        /** Scrolls so the section titled `name` (e.g. "destination") is at the top, as far as
            the content allows. Returns false if there is no such section. */
        bool scrollToSection (const juce::String& name);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Content final : public juce::Component
        {
        public:
            Content();

            int getPreferredHeight() const;
            void resized() override;

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
