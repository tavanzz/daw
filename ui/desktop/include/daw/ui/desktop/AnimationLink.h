#pragma once

#include "daw/appearance/AnimationEffect.h"
#include "daw/ui/desktop/AnimationClock.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui::desktop
{

/**
    A component's frames from the shared animation clock for one effect, while the link
    lives. The clock is the one of the component's themed look and feel (none outside
    the app: then the effect is never active and no frame comes).

    Message thread.
*/
class AnimationLink final : private AnimationClock::Client
{
public:
    using FrameCallback = std::function<void(double seconds)>;

    /** Subscribes at once; the link wants frames until told otherwise. */
    AnimationLink(const juce::Component& owner, appearance::AnimationEffect effect,
                  FrameCallback onFrame);
    ~AnimationLink() override;

    AnimationLink(const AnimationLink&) = delete;
    AnimationLink& operator=(const AnimationLink&) = delete;

    void setWantsFrames(bool wantsFrames);

    /** Whether the effect is drawn at all now (else draw as without animation). */
    [[nodiscard]] bool isActive() const;

    /** Seconds on the shared clock (0 without one). */
    [[nodiscard]] double getTime() const;

private:
    void animationFrame(double seconds) override;

    AnimationClock* clock = nullptr;
    const appearance::AnimationEffect effect;
    FrameCallback callback;
    bool wants = true;
};

} // namespace daw::ui::desktop
