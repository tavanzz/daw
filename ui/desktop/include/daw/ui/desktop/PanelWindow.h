#pragma once

#include "daw/ui/desktop/AnimationLink.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/** A small floating window for a panel (theme previews, animation settings), in the
    current theme. It fades in when panel transitions are on; closing it calls onClose,
    which may delete it. */
class PanelWindow final : public juce::DocumentWindow
{
public:
    PanelWindow(const juce::String& title, std::unique_ptr<juce::Component> content,
                juce::Component* centreAround, std::function<void()> onClose);
    ~PanelWindow() override;

    void closeButtonPressed() override;

private:
    void fadeFrame(double seconds);

    std::function<void()> closed;
    double fadeStart = -1.0;
    AnimationLink fade{*this, appearance::AnimationEffect::panelTransitions, [this](double seconds)
                       {
                           fadeFrame(seconds);
                       }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelWindow)
};

} // namespace daw::ui::desktop
