#pragma once

#include "daw/ui/desktop/AnimationLink.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/** The band under the transport in themes that have one (Acid Terminal's hazard
    stripes), scrolling with the moving backgrounds effect, faster while recording. */
class HazardBand final : public juce::Component
{
public:
    HazardBand();

    void setRecording(bool isRecording);

    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

private:
    bool recording = false;
    AnimationLink stripes{*this, appearance::AnimationEffect::movingBackgrounds, [this](double)
                          {
                              repaint();
                          }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HazardBand)
};

/** The window as it looked in the previous theme, fading out over the new one. */
class ThemeCrossfade final : public juce::Component
{
public:
    static constexpr double seconds = 0.3;

    /** Calls onFinished (which may delete it) once the old look has faded away. */
    ThemeCrossfade(juce::Image oldLook, std::function<void()> onFinished);

    void paint(juce::Graphics& g) override;

private:
    juce::Image image;
    std::function<void()> finished;
    double start = -1.0;
    float opacity = 1.0f;
    AnimationLink fade{*this, appearance::AnimationEffect::panelTransitions, [this](double now)
                       {
                           advance(now);
                       }};

    void advance(double now);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeCrossfade)
};

} // namespace daw::ui::desktop
