#include "Sidebar.h"
#include "Theme.h"

namespace rf::ui
{
    Sidebar::Content::Content()
    {
        for (auto* section : sections)
            addAndMakeVisible (section);
    }

    int Sidebar::Content::getPreferredHeight() const
    {
        int height = 0;

        for (auto* section : sections)
            height += section->getPreferredHeight();

        return height;
    }

    void Sidebar::Content::resized()
    {
        auto area = getLocalBounds();

        for (auto* section : sections)
            section->setBounds (area.removeFromTop (section->getPreferredHeight()));
    }

    //==============================================================================
    Sidebar::Sidebar()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (theme::metric::grid);
        addAndMakeVisible (viewport);
    }

    void Sidebar::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::bg);

        // 1 px border separating the sidebar from the main area.
        g.setColour (theme::colour::line);
        g.fillRect (0, 0, 1, getHeight());
    }

    void Sidebar::resized()
    {
        viewport.setBounds (getLocalBounds().withTrimmedLeft (1));

        const auto contentHeight = content.getPreferredHeight();
        const auto needsScroll = contentHeight > viewport.getHeight();
        const auto width = viewport.getWidth() - (needsScroll ? viewport.getScrollBarThickness() : 0);

        content.setSize (width, contentHeight);
    }
}
