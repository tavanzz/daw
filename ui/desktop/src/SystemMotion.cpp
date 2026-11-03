#include "daw/ui/desktop/SystemMotion.h"

#include "SystemQueries.h"

namespace daw::ui::desktop
{

SystemMotion::SystemMotion(Callback onChange) : callback(std::move(onChange))
{
    refresh();
}

SystemMotion::~SystemMotion()
{
    *alive = false;
}

void SystemMotion::refresh()
{
#if JUCE_LINUX || JUCE_BSD
    // gsettings is a separate process: ask from another thread, answer on this one.
    std::weak_ptr<bool> weakAlive = alive;
    juce::Thread::launch(
        [this, weakAlive]
        {
            const bool asks = detail::querySystemReducesMotion();
            juce::MessageManager::callAsync(
                [this, weakAlive, asks]
                {
                    if (const auto stillAlive = weakAlive.lock(); stillAlive && *stillAlive)
                        received(asks);
                });
        });
#else
    received(detail::querySystemReducesMotion());
#endif
}

void SystemMotion::received(bool asksToReduce)
{
    if (asksToReduce == reduceMotion)
        return;

    reduceMotion = asksToReduce;
    if (callback)
        callback(reduceMotion);
}

} // namespace daw::ui::desktop
