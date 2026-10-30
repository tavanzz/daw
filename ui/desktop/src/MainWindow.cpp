#include "daw/ui/desktop/MainWindow.h"

#include "daw/ui/desktop/MainComponent.h"

namespace daw::ui::desktop
{

MainWindow::MainWindow(const juce::String& windowTitle)
    : DocumentWindow(windowTitle,
                     juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                         juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
}

} // namespace daw::ui::desktop
