#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** The app's name at the left of the transport, in the theme's logo style. */
class Logo final : public juce::Component
{
public:
    Logo();

    void paint(juce::Graphics& g) override;

private:
    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Logo)
};

} // namespace daw::ui::desktop
