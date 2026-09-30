#include "MainWindow.h"
#include "MainComponent.h"
#include "../UI/Theme.h"

namespace rf::app
{
    namespace metric = ui::theme::metric;

    MainWindow::MainWindow (const juce::String& name)
        : DocumentWindow (name, ui::theme::colour::bg, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);

        setResizable (true, false);
        setResizeLimits (metric::minWindowWidth, metric::minWindowHeight, 16384, 16384);
        centreWithSize (getWidth(), getHeight());

        setVisible (true);
    }

    void MainWindow::closeButtonPressed()
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}
