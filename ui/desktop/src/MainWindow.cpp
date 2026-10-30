#include "daw/ui/desktop/MainWindow.h"

namespace daw::ui::desktop
{

MainWindow::MainWindow(const juce::String& windowTitle, engine::Engine& engine,
                       juce::AudioDeviceManager& deviceManager)
    : DocumentWindow(windowTitle,
                     juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                         juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    auto content = std::make_unique<MainComponent>(engine, deviceManager, commandManager);
    mainComponent = content.get();

    commandManager.registerAllCommandsForTarget(mainComponent);
    commandManager.registerAllCommandsForTarget(juce::JUCEApplication::getInstance());
    commandManager.setFirstCommandTarget(mainComponent);
    addKeyListener(commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch(&commandManager);

    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), true);
    setResizable(true, true);
    setResizeLimits(640, 360, 10000, 10000);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#else
    setMenuBar(this);
#endif

    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

MainWindow::~MainWindow()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    setMenuBar(nullptr);
#endif
    // The command manager is destroyed right after this body: detach everything
    // that refers to it first.
    setApplicationCommandManagerToWatch(nullptr);
    removeKeyListener(commandManager.getKeyMappings());
    clearContentComponent();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return {"File", "Transport"};
}

juce::PopupMenu MainWindow::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addCommandItem(&commandManager, MainComponent::openAudioFile);
        menu.addSeparator();
        menu.addCommandItem(&commandManager, MainComponent::openProject);
        menu.addCommandItem(&commandManager, MainComponent::saveProject);
        menu.addCommandItem(&commandManager, MainComponent::saveProjectAs);
        menu.addSeparator();
        menu.addCommandItem(&commandManager, MainComponent::audioSettings);
#if !JUCE_MAC
        menu.addSeparator();
        menu.addCommandItem(&commandManager, juce::StandardApplicationCommandIDs::quit);
#endif
    }
    else if (menuIndex == 1)
    {
        menu.addCommandItem(&commandManager, MainComponent::playPause);
        menu.addCommandItem(&commandManager, MainComponent::stop);
        menu.addSeparator();
        menu.addCommandItem(&commandManager, MainComponent::toggleLoop);
    }

    return menu;
}

void MainWindow::menuItemSelected(int, int)
{
    // Every item is a command; the command manager runs it.
}

} // namespace daw::ui::desktop
