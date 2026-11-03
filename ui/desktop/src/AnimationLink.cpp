#include "daw/ui/desktop/AnimationLink.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

AnimationLink::AnimationLink(const juce::Component& owner, appearance::AnimationEffect effectToDraw,
                             FrameCallback onFrame)
    : clock(ThemedLookAndFeel::get(owner).getAnimationClock()), effect(effectToDraw),
      callback(std::move(onFrame))
{
    if (clock != nullptr)
        clock->subscribe(*this, effect);
}

AnimationLink::~AnimationLink()
{
    if (clock != nullptr)
        clock->unsubscribe(*this);
}

void AnimationLink::setWantsFrames(bool wantsFrames)
{
    if (wants == wantsFrames)
        return;

    wants = wantsFrames;
    if (clock != nullptr)
        clock->setWantsFrames(*this, wantsFrames);
}

bool AnimationLink::isActive() const
{
    return clock != nullptr && clock->isActive(effect);
}

double AnimationLink::getTime() const
{
    return clock != nullptr ? clock->getTime() : 0.0;
}

void AnimationLink::animationFrame(double seconds)
{
    if (callback)
        callback(seconds);
}

} // namespace daw::ui::desktop
