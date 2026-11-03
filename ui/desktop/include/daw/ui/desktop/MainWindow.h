#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/ThemeManager.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::ui::desktop
{

namespace detail
{
class WindowOcclusion;
}

/** Top-level desktop window with the menu bar (the macOS main menu, or a bar inside
    the window elsewhere) and keyboard shortcuts. Closing it asks the application to
    quit. It tells the animation clock whether it can be seen (not minimised, hidden or
    covered), and reads the system's reduce motion again when it becomes active. */
class MainWindow final : public juce::DocumentWindow, public juce::MenuBarModel
{
public:
    MainWindow(const juce::String& windowTitle, controller::Session& session,
               juce::AudioDeviceManager& deviceManager, ThemeManager& themeManager);
    ~MainWindow() override;

    [[nodiscard]] MainComponent& getMainComponent() noexcept { return *mainComponent; }

    void closeButtonPressed() override;
    void minimisationStateChanged(bool isNowMinimised) override;
    void visibilityChanged() override;
    void activeWindowStatusChanged() override;

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemId, int menuIndex) override;

private:
    void updateAnimationVisibility();

    ThemeManager& themes;
    std::unique_ptr<detail::WindowOcclusion> occlusion;
    juce::ApplicationCommandManager commandManager;
    MainComponent* mainComponent = nullptr; // owned by the window as its content

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::ui::desktop
