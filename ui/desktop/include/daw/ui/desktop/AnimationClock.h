#pragma once

#include "daw/appearance/AnimationEffect.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <vector>

namespace daw::ui::desktop
{

/**
    The one timer behind every decorative animation (meters, playhead glow, record
    pulse, moving backgrounds, glitches, panel transitions). Animated components
    subscribe for the effect they draw and repaint only what changes when called.

    It ticks only while at least one subscriber draws an effect that is active (the
    theme draws it, its switch is on, the level allows it) and wants frames, and the
    main window can be seen; otherwise no timer runs at all. The functional refresh
    (meters' levels, the playhead's position, the status) is separate: it must go on
    with animations off.

    Message thread.
*/
class AnimationClock final : private juce::Timer
{
public:
    static constexpr int framesPerSecond = 30;

    class Client
    {
    public:
        virtual ~Client() = default;

        /** A frame: seconds on the clock, shared by every animation so they keep in step. */
        virtual void animationFrame(double seconds) = 0;
    };

    /** Whether an effect is active now (ThemeManager::isEffectActive()). */
    using EffectFilter = std::function<bool(appearance::AnimationEffect)>;

    explicit AnimationClock(EffectFilter isEffectActive);
    ~AnimationClock() override;

    /** Frames for an effect; the client wants frames until it says otherwise. */
    void subscribe(Client& client, appearance::AnimationEffect effect);
    void unsubscribe(Client& client);

    /** A client with nothing to animate for now (a playhead glow while stopped) stops
        asking for frames, and asks again later. */
    void setWantsFrames(Client& client, bool wantsFrames);

    /** Whether the main window can be seen: false while it is minimised, hidden or
        completely covered. */
    void setHostVisible(bool isVisible);

    /** Call after the animation settings, the theme or the system's reduce motion
        changed: effects may have started or stopped. */
    void effectsChanged();

    /** Whether to draw the effect at all (as it would be without animation if not). */
    [[nodiscard]] bool isActive(appearance::AnimationEffect effect) const;

    [[nodiscard]] bool isRunning() const noexcept { return isTimerRunning(); }
    [[nodiscard]] bool isHostVisible() const noexcept { return hostVisible; }

    /** Seconds since the clock was made. */
    [[nodiscard]] double getTime() const noexcept;

    /** One frame now, as the timer would run it (tests). */
    void runFrame();

private:
    struct Subscription
    {
        Client* client = nullptr;
        appearance::AnimationEffect effect = appearance::AnimationEffect::animatedMeters;
        bool wantsFrames = true;
    };

    void timerCallback() override;
    void updateTimer();
    [[nodiscard]] bool needsFrames(const Subscription& subscription) const;

    EffectFilter effectFilter;
    std::vector<Subscription> subscriptions;
    bool hostVisible = true;
    double startSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationClock)
};

} // namespace daw::ui::desktop
