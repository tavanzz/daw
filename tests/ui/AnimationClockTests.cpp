#include "daw/ui/desktop/AnimationClock.h"
#include "daw/ui/desktop/ThemeManager.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace daw;
using appearance::AnimationEffect;
using ui::desktop::AnimationClock;

namespace
{
class CountingClient final : public AnimationClock::Client
{
public:
    void animationFrame(double) override { ++frames; }

    int frames = 0;
};

// Effects switched on and off by the test, as the animation settings would.
struct EffectSwitches
{
    std::set<AnimationEffect> active{AnimationEffect::cursorGlow, AnimationEffect::glitch};

    AnimationClock::EffectFilter filter()
    {
        return [this](AnimationEffect effect)
        {
            return active.count(effect) != 0;
        };
    }
};
} // namespace

TEST_CASE("The animation clock runs only while an active effect wants frames", "[ui]")
{
    EffectSwitches switches;
    AnimationClock clock(switches.filter());
    CountingClient glow;
    CountingClient meters;

    CHECK_FALSE(clock.isRunning());

    clock.subscribe(meters, AnimationEffect::animatedMeters); // not active
    CHECK_FALSE(clock.isRunning());

    clock.subscribe(glow, AnimationEffect::cursorGlow);
    CHECK(clock.isRunning());

    clock.setWantsFrames(glow, false);
    CHECK_FALSE(clock.isRunning());
    clock.setWantsFrames(glow, true);
    CHECK(clock.isRunning());

    switches.active.clear();
    clock.effectsChanged();
    CHECK_FALSE(clock.isRunning());

    switches.active.insert(AnimationEffect::animatedMeters);
    clock.effectsChanged();
    CHECK(clock.isRunning());

    clock.unsubscribe(meters);
    clock.unsubscribe(glow);
    CHECK_FALSE(clock.isRunning());
}

TEST_CASE("The animation clock stops while the window cannot be seen", "[ui]")
{
    EffectSwitches switches;
    AnimationClock clock(switches.filter());
    CountingClient glow;
    clock.subscribe(glow, AnimationEffect::cursorGlow);
    REQUIRE(clock.isRunning());

    clock.setHostVisible(false);
    CHECK_FALSE(clock.isRunning());

    clock.setHostVisible(true);
    CHECK(clock.isRunning());

    clock.unsubscribe(glow);
}

TEST_CASE("Frames go only to clients whose effect is active and who want them", "[ui]")
{
    EffectSwitches switches;
    AnimationClock clock(switches.filter());
    CountingClient glow;
    CountingClient glitch;
    CountingClient meters;
    clock.subscribe(glow, AnimationEffect::cursorGlow);
    clock.subscribe(glitch, AnimationEffect::glitch);
    clock.subscribe(meters, AnimationEffect::animatedMeters);
    clock.setWantsFrames(glitch, false);

    clock.runFrame();
    clock.runFrame();

    CHECK(glow.frames == 2);
    CHECK(glitch.frames == 0);
    CHECK(meters.frames == 0);
    CHECK(clock.isActive(AnimationEffect::cursorGlow));
    CHECK_FALSE(clock.isActive(AnimationEffect::animatedMeters));

    clock.unsubscribe(glow);
    clock.unsubscribe(glitch);
    clock.unsubscribe(meters);
}

TEST_CASE("A client may leave the clock from its own frame", "[ui]")
{
    EffectSwitches switches;
    AnimationClock clock(switches.filter());

    struct LeavingClient final : AnimationClock::Client
    {
        AnimationClock* owner = nullptr;
        AnimationClock::Client* neighbour = nullptr;
        int frames = 0;

        void animationFrame(double) override
        {
            ++frames;
            owner->unsubscribe(*this);
            owner->unsubscribe(*neighbour);
        }
    };

    CountingClient other;
    LeavingClient leaving;
    leaving.owner = &clock;
    leaving.neighbour = &other;
    clock.subscribe(leaving, AnimationEffect::cursorGlow);
    clock.subscribe(other, AnimationEffect::cursorGlow);

    clock.runFrame();

    CHECK(leaving.frames == 1);
    CHECK(other.frames == 0);
    CHECK_FALSE(clock.isRunning());
}

TEST_CASE("The theme manager's clock follows the level and the system's reduce motion", "[ui]")
{
    ui::desktop::ThemeManager themes({});
    themes.setSystemReducesMotionForTesting(false);
    auto& clock = themes.getAnimationClock();

    CountingClient glitch;
    CountingClient glow;
    clock.subscribe(glitch, AnimationEffect::glitch);
    CHECK(clock.isRunning());

    auto settings = themes.getPreferences().animations;
    settings.setLevel(appearance::AnimationLevel::reduced);
    themes.setAnimationSettings(settings);
    CHECK_FALSE(clock.isRunning());

    settings.setLevel(appearance::AnimationLevel::full);
    themes.setAnimationSettings(settings);
    CHECK(clock.isRunning());

    // Following the system: reduce motion caps full at reduced.
    themes.setSystemReducesMotionForTesting(true);
    CHECK_FALSE(clock.isRunning());
    clock.subscribe(glow, AnimationEffect::cursorGlow);
    CHECK(clock.isRunning());

    // Switch all off (Shift+Cmd+A): nothing runs.
    settings.toggleAllOff();
    themes.setAnimationSettings(settings);
    CHECK_FALSE(clock.isRunning());
    CHECK_FALSE(themes.isEffectActive(AnimationEffect::cursorGlow));

    settings.toggleAllOff();
    settings.followSystemReduceMotion = false;
    themes.setAnimationSettings(settings);
    CHECK(themes.isEffectActive(AnimationEffect::glitch));

    clock.unsubscribe(glitch);
    clock.unsubscribe(glow);
}
