#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui::desktop
{

/** Shows a message box in the current theme: JUCE's alert window, drawn by the look
    and feel, instead of the system's. The callback gets the index of the button that
    was clicked, the first being 0, as NativeMessageBox gives it; Escape counts as the
    last button (Cancel). */
void showAlert(const juce::MessageBoxOptions& options,
               std::function<void(int buttonIndex)> callback = {});

/** The button index for an alert window's result (button X returns (X + 1) % N). */
[[nodiscard]] int alertButtonIndex(int alertResult, int numButtons) noexcept;

} // namespace daw::ui::desktop
