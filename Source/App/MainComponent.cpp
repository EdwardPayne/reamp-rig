#include "MainComponent.h"
#include "../UI/Theme.h"

namespace rf::app
{
    namespace metric = ui::theme::metric;

    MainComponent::MainComponent()
    {
        splitLayout.setItemLayout (1, metric::splitterSize, metric::splitterSize, metric::splitterSize);

        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &fileTree, &splitter,
                                                                 &waveformPanel, &sidebar, &statusBar })
            addAndMakeVisible (c);

        setSize (1280, 800);
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (ui::theme::colour::bg);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();

        topBar.setBounds (area.removeFromTop (metric::topBarHeight));
        statusBar.setBounds (area.removeFromBottom (metric::statusBarHeight));
        sidebar.setBounds (area.removeFromRight (metric::sidebarWidth));

        layOutSplit (area);
    }

    void MainComponent::layOutSplit (juce::Rectangle<int> area)
    {
        // Keep the waveform panel at its current (or dragged) height when the window resizes;
        // the file tree takes the rest.
        if (const auto current = splitLayout.getItemCurrentAbsoluteSize (2); current > 0)
            waveformHeight = current;

        const auto maxWaveform = juce::jmax (metric::waveformMinHeight,
                                             area.getHeight() - metric::splitterSize - metric::fileTreeMinHeight);
        waveformHeight = juce::jlimit (metric::waveformMinHeight, maxWaveform, waveformHeight);

        const auto fileTreeHeight = area.getHeight() - metric::splitterSize - waveformHeight;
        splitLayout.setItemLayout (0, metric::fileTreeMinHeight, -1.0, fileTreeHeight);
        splitLayout.setItemLayout (2, metric::waveformMinHeight, -1.0, waveformHeight);

        juce::Component* stack[] = { &fileTree, &splitter, &waveformPanel };
        splitLayout.layOutComponents (stack, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                                      true, true);
    }
}
