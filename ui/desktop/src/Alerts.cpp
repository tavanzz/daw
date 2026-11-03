#include "daw/ui/desktop/Alerts.h"

namespace daw::ui::desktop
{

int alertButtonIndex(int alertResult, int numButtons) noexcept
{
    if (numButtons <= 0)
        return 0;

    return ((alertResult - 1) % numButtons + numButtons) % numButtons;
}

void showAlert(const juce::MessageBoxOptions& options,
               std::function<void(int buttonIndex)> callback)
{
    const int numButtons = options.getNumButtons();
    juce::AlertWindow::showAsync(options,
                                 [numButtons, callback = std::move(callback)](int result)
                                 {
                                     if (callback)
                                         callback(alertButtonIndex(result, numButtons));
                                 });
}

} // namespace daw::ui::desktop
