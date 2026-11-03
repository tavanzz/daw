#include "daw/ui/desktop/AnimationClock.h"

#include <algorithm>

namespace daw::ui::desktop
{

AnimationClock::AnimationClock(EffectFilter isEffectActive)
    : effectFilter(std::move(isEffectActive)),
      startSeconds(juce::Time::getMillisecondCounterHiRes() / 1000.0)
{
}

AnimationClock::~AnimationClock()
{
    stopTimer();
}

void AnimationClock::subscribe(Client& client, appearance::AnimationEffect effect)
{
    unsubscribe(client);
    subscriptions.push_back({&client, effect, true});
    updateTimer();
}

void AnimationClock::unsubscribe(Client& client)
{
    std::erase_if(subscriptions, [&client](const Subscription& subscription)
                  { return subscription.client == &client; });
    updateTimer();
}

void AnimationClock::setWantsFrames(Client& client, bool wantsFrames)
{
    for (auto& subscription : subscriptions)
        if (subscription.client == &client)
            subscription.wantsFrames = wantsFrames;

    updateTimer();
}

void AnimationClock::setHostVisible(bool isVisible)
{
    hostVisible = isVisible;
    updateTimer();
}

void AnimationClock::effectsChanged()
{
    updateTimer();
}

bool AnimationClock::isActive(appearance::AnimationEffect effect) const
{
    return effectFilter != nullptr && effectFilter(effect);
}

double AnimationClock::getTime() const noexcept
{
    return juce::Time::getMillisecondCounterHiRes() / 1000.0 - startSeconds;
}

bool AnimationClock::needsFrames(const Subscription& subscription) const
{
    return subscription.wantsFrames && isActive(subscription.effect);
}

void AnimationClock::updateTimer()
{
    const bool anyNeedsFrames =
        std::any_of(subscriptions.begin(), subscriptions.end(),
                    [this](const Subscription& subscription) { return needsFrames(subscription); });

    if (hostVisible && anyNeedsFrames)
    {
        if (!isTimerRunning())
            startTimerHz(framesPerSecond);
    }
    else
    {
        stopTimer();
    }
}

void AnimationClock::runFrame()
{
    const double seconds = getTime();

    // A client may unsubscribe (or subscribe another) from its frame: walk a copy.
    const auto current = subscriptions;
    for (const auto& subscription : current)
    {
        const bool stillSubscribed = std::any_of(subscriptions.begin(), subscriptions.end(),
                                                 [&subscription](const Subscription& other)
                                                 { return other.client == subscription.client; });

        if (stillSubscribed && needsFrames(subscription))
            subscription.client->animationFrame(seconds);
    }
}

void AnimationClock::timerCallback()
{
    runFrame();
}

} // namespace daw::ui::desktop
