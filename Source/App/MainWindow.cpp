#include "MainWindow.h"
#include "MainComponent.h"
#include "../UI/Theme.h"

namespace rf::app
{
    namespace metric = ui::theme::metric;

    MainWindow::MainWindow (const juce::String& name, Settings& settings, const LaunchOptions& options)
        : DocumentWindow (name, ui::theme::colour::bg, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (settings, options), true);

        setResizable (true, false);
        setResizeLimits (metric::minWindowWidth, metric::minWindowHeight, 16384, 16384);
        centreWithSize (getWidth(), getHeight());

        setVisible (true);
    }

    MainComponent& MainWindow::getMainComponent()
    {
        auto* content = dynamic_cast<MainComponent*> (getContentComponent());
        jassert (content != nullptr);
        return *content;
    }

    void MainWindow::closeButtonPressed()
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}
