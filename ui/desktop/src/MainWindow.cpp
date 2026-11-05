#include "daw/ui/desktop/MainWindow.h"

#include "SystemQueries.h"

namespace daw::ui::desktop
{

MainWindow::MainWindow(const juce::String& windowTitle, controller::Session& session,
                       juce::AudioDeviceManager& deviceManager, ThemeManager& themeManager)
    : DocumentWindow(windowTitle,
                     juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                         juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
    , themes(themeManager)
{
    auto content =
        std::make_unique<MainComponent>(session, deviceManager, commandManager, themeManager);
    mainComponent = content.get();

    commandManager.registerAllCommandsForTarget(mainComponent);
    commandManager.registerAllCommandsForTarget(juce::JUCEApplication::getInstance());
    commandManager.setFirstCommandTarget(mainComponent);
    addKeyListener(commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch(&commandManager);

    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), true);
    setResizable(true, true);
    setResizeLimits(1100, 560, 10000, 10000);

#if JUCE_MAC
    // The macOS menu bar belongs to the application (none in the UI tests).
    if (juce::JUCEApplicationBase::getInstance() != nullptr)
        juce::MenuBarModel::setMacMainMenu(this);
#else
    setMenuBar(this);
#endif

    centreWithSize(getWidth(), getHeight());
    setVisible(true);

    occlusion = std::make_unique<detail::WindowOcclusion>(*this, [this]
                                                          { updateAnimationVisibility(); });
    updateAnimationVisibility();
}

MainWindow::~MainWindow()
{
    occlusion.reset();

#if JUCE_MAC
    if (juce::JUCEApplicationBase::getInstance() != nullptr)
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

void MainWindow::minimisationStateChanged(bool isNowMinimised)
{
    DocumentWindow::minimisationStateChanged(isNowMinimised);
    updateAnimationVisibility();
}

void MainWindow::visibilityChanged()
{
    DocumentWindow::visibilityChanged();
    updateAnimationVisibility();
}

void MainWindow::activeWindowStatusChanged()
{
    DocumentWindow::activeWindowStatusChanged();

    // The user may have changed the system's reduce motion while away.
    if (isActiveWindow())
        themes.refreshSystemMotion();
}

void MainWindow::updateAnimationVisibility()
{
    const bool covered = occlusion != nullptr && occlusion->isCovered();
    themes.getAnimationClock().setHostVisible(isShowing() && !isMinimised() && !covered);
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
        add(MainComponent::exportMidi);
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
        add(MainComponent::duplicate);
        add(MainComponent::deleteSelection);
        menu.addSeparator();
        add(MainComponent::quantiseNotes);
        break;
    case 2:
        add(MainComponent::newTrack);
        add(MainComponent::newMidiTrack);
        add(MainComponent::newBus);
        break;
    case 3:
        add(MainComponent::playPause);
        add(MainComponent::stop);
        add(MainComponent::record);
        add(MainComponent::discardRecording);
        menu.addSeparator();
        add(MainComponent::toggleLoop);
        add(MainComponent::toggleSnap);
        menu.addSeparator();
        add(MainComponent::toggleMetronome);
        add(MainComponent::toggleCountIn);
        add(MainComponent::togglePunch);
        break;
    case 4:
        add(MainComponent::zoomIn);
        add(MainComponent::zoomOut);
        add(MainComponent::zoomToFit);
        menu.addSeparator();
        add(MainComponent::toggleMixer);
        add(MainComponent::togglePianoRoll);
        add(MainComponent::toggleAutomation);
        menu.addSeparator();
        {
            juce::PopupMenu themeMenu;
            const auto numThemes =
                static_cast<int>(themes.getLibrary().getThemes().size());
            for (int index = 0; index < numThemes &&
                                MainComponent::firstTheme + index <= MainComponent::lastTheme;
                 ++index)
                themeMenu.addCommandItem(&commandManager, MainComponent::firstTheme + index);
            themeMenu.addSeparator();
            themeMenu.addCommandItem(&commandManager, MainComponent::showThemePreviews);
            menu.addSubMenu("Theme", themeMenu);
        }
        add(MainComponent::showAnimations);
        add(MainComponent::toggleAllAnimations);
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
