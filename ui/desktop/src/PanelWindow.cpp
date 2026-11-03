#include "daw/ui/desktop/PanelWindow.h"

#include <algorithm>

namespace daw::ui::desktop
{

namespace
{
constexpr double fadeSeconds = 0.15;
}

PanelWindow::PanelWindow(const juce::String& title, std::unique_ptr<juce::Component> content,
                         juce::Component* centreAround, std::function<void()> onClose)
    : juce::DocumentWindow(title,
                           juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                               juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::closeButton),
      closed(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), true);
    setResizable(false, false);

    if (centreAround != nullptr)
        centreAroundComponent(centreAround, getWidth(), getHeight());
    else
        centreWithSize(getWidth(), getHeight());

    // Faded in by the clock when transitions are on; shown at once otherwise.
    if (fade.isActive())
        getContentComponent()->setAlpha(0.0f);
    else
        fade.setWantsFrames(false);

    setVisible(true);
}

PanelWindow::~PanelWindow() = default;

void PanelWindow::closeButtonPressed()
{
    if (closed)
        closed();
}

void PanelWindow::fadeFrame(double seconds)
{
    if (fadeStart < 0.0)
        fadeStart = seconds;

    const auto progress = std::min(1.0, (seconds - fadeStart) / fadeSeconds);
    getContentComponent()->setAlpha(static_cast<float>(progress));

    if (progress >= 1.0 || !fade.isActive())
    {
        getContentComponent()->setAlpha(1.0f);
        fade.setWantsFrames(false);
    }
}

} // namespace daw::ui::desktop
