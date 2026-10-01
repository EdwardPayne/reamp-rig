#include "MainWindow.h"
#include "MainComponent.h"
#include "../UI/Theme.h"

namespace rf::app
{
    namespace metric = ui::theme::metric;

    MainWindow::MainWindow (const juce::String& name, Settings& s, const LaunchOptions& options)
        : DocumentWindow (name, ui::theme::colour::bg, DocumentWindow::allButtons),
          settings (s)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (settings, options), true);

        setResizable (true, false);
        setResizeLimits (metric::minWindowWidth, metric::minWindowHeight, 16384, 16384);
        centreWithSize (getWidth(), getHeight());

        setVisible (true);
        restoreBounds();     // needs the native frame size, known once the window is on screen

        // Development aid: move/resize as if the user dragged the window (it is then saved).
        if (options.windowBounds.has_value())
            setBounds (*options.windowBounds);
    }

    MainWindow::~MainWindow()
    {
        saveBounds();
    }

    MainComponent& MainWindow::getMainComponent()
    {
        auto* content = dynamic_cast<MainComponent*> (getContentComponent());
        jassert (content != nullptr);
        return *content;
    }

    void MainWindow::closeButtonPressed()
    {
        saveBounds();
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    juce::BorderSize<int> MainWindow::getFrame() const
    {
        if (auto* peer = getPeer())
            if (const auto frame = peer->getFrameSizeIfPresent())
                return *frame;

        return {};
    }

    void MainWindow::restoreBounds()
    {
        const auto saved = settings.getWindowBounds();
        boundsRestored = true;

        if (! saved.has_value())
        {
            juce::Logger::writeToLog ("Window: no saved bounds, centred at " + getBounds().toString());
            saveBounds();
            return;
        }

        // The main display first: a window whose display is gone lands there.
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        juce::Array<juce::Rectangle<int>> areas;

        if (const auto* main = displays.getPrimaryDisplay())
            areas.add (main->userBounds.getSmallestIntegerContainer());

        for (const auto& d : displays.displays)
            if (! areas.contains (d.userBounds.getSmallestIntegerContainer()))
                areas.add (d.userBounds.getSmallestIntegerContainer());

        const auto frame = getFrame();
        const juce::Point<int> minFrame (metric::minWindowWidth + frame.getLeftAndRight(),
                                         metric::minWindowHeight + frame.getTopAndBottom());

        if (const auto clamped = clampWindowBounds (*saved, areas, minFrame))
        {
            setBounds (frame.subtractedFrom (*clamped));
            juce::Logger::writeToLog ("Window: restored " + getBounds().toString() + " (saved frame " + saved->toString()
                                      + (*clamped != *saved ? ", clamped to " + clamped->toString() : juce::String()) + ")");
        }

        saveBounds();
    }

    void MainWindow::saveBounds()
    {
        if (! boundsRestored || isFullScreen() || isMinimised() || ! isOnDesktop())
            return;

        settings.setWindowBounds (getFrame().addedTo (getBounds()));
    }

    void MainWindow::moved()
    {
        DocumentWindow::moved();
        saveBounds();
    }

    void MainWindow::resized()
    {
        DocumentWindow::resized();
        saveBounds();
    }
}
