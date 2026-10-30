#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/MainComponent.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** Top-level desktop window with the menu bar (the macOS main menu, or a bar inside
    the window elsewhere) and keyboard shortcuts. Closing it asks the application to
    quit. */
class MainWindow final : public juce::DocumentWindow, public juce::MenuBarModel
{
public:
    MainWindow(const juce::String& windowTitle, controller::Session& session,
               juce::AudioDeviceManager& deviceManager);
    ~MainWindow() override;

    [[nodiscard]] MainComponent& getMainComponent() noexcept { return *mainComponent; }

    void closeButtonPressed() override;

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemId, int menuIndex) override;

private:
    juce::ApplicationCommandManager commandManager;
    MainComponent* mainComponent = nullptr; // owned by the window as its content

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::ui::desktop
