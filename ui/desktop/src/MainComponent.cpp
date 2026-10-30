#include "daw/ui/desktop/MainComponent.h"

namespace daw::ui::desktop
{

MainComponent::MainComponent()
{
    setSize(900, 600);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(16.0f));
    g.drawText("No audio file loaded", getLocalBounds(), juce::Justification::centred);
}

} // namespace daw::ui::desktop
