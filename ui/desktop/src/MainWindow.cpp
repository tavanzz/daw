#include "daw/ui/desktop/MainWindow.h"

namespace daw::ui::desktop
{

MainWindow::MainWindow(const juce::String& windowTitle, controller::Session& session,
                       juce::AudioDeviceManager& deviceManager)
    : DocumentWindow(windowTitle,
                     juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                         juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    auto content = std::make_unique<MainComponent>(session, deviceManager, commandManager);
    mainComponent = content.get();

    commandManager.registerAllCommandsForTarget(mainComponent);
    commandManager.registerAllCommandsForTarget(juce::JUCEApplication::getInstance());
    commandManager.setFirstCommandTarget(mainComponent);
    addKeyListener(commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch(&commandManager);

    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), true);
    setResizable(true, true);
    setResizeLimits(800, 480, 10000, 10000);

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
    return {"File", "Edit", "Track", "Transport", "View"};
}

juce::PopupMenu MainWindow::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    const auto add = [this, &menu](juce::CommandID command) {
        menu.addCommandItem(&commandManager, command);
    };

    switch (menuIndex)
    {
    case 0:
        add(MainComponent::newProject);
        add(MainComponent::openProject);
        add(MainComponent::saveProject);
        add(MainComponent::saveProjectAs);
        menu.addSeparator();
        add(MainComponent::importAudio);
        add(MainComponent::exportMix);
        menu.addSeparator();
        add(MainComponent::audioSettings);
#if !JUCE_MAC
        menu.addSeparator();
        add(juce::StandardApplicationCommandIDs::quit);
#endif
        break;
    case 1:
        add(MainComponent::undo);
        add(MainComponent::redo);
        menu.addSeparator();
        add(MainComponent::deleteSelection);
        break;
    case 2:
        add(MainComponent::newTrack);
        break;
    case 3:
        add(MainComponent::playPause);
        add(MainComponent::stop);
        menu.addSeparator();
        add(MainComponent::toggleLoop);
        add(MainComponent::toggleSnap);
        break;
    case 4:
        add(MainComponent::zoomIn);
        add(MainComponent::zoomOut);
        add(MainComponent::zoomToFit);
        menu.addSeparator();
        add(MainComponent::toggleMixer);
        break;
    default:
        break;
    }

    return menu;
}

void MainWindow::menuItemSelected(int, int)
{
    // Every item is a command; the command manager runs it.
}

} // namespace daw::ui::desktop
