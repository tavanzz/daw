#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** Content of the main desktop window. */
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace daw::ui::desktop
