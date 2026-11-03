#pragma once

#include "daw/ui/desktop/AnimationLink.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** The app's name at the left of the transport, in the theme's logo style, with the
    theme's glitch when that effect is on. */
class Logo final : public juce::Component
{
public:
    Logo();

    void paint(juce::Graphics& g) override;

private:
    juce::String text;
    AnimationLink glitch{*this, appearance::AnimationEffect::glitch, [this](double)
                         {
                             repaint();
                         }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Logo)
};

} // namespace daw::ui::desktop
