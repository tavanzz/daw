#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** Top-level desktop window. Closing it asks the application to quit. */
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& windowTitle);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::ui::desktop
