#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/**
    Whether the operating system asks apps to reduce motion: macOS (Accessibility >
    Display > Reduce motion), Windows (Accessibility > Visual effects > Animation
    effects off), Linux (GNOME's "enable-animations" off; other desktops never ask).

    Read at start and again with refresh() (the app calls it when its window becomes
    active, since the user may just have changed the setting). Message thread.
*/
class SystemMotion final
{
public:
    using Callback = std::function<void(bool asksToReduceMotion)>;

    /** Reads the setting now (on Linux the answer arrives a moment later). */
    explicit SystemMotion(Callback onChange);
    ~SystemMotion();

    /** Reads the setting again and calls back if it changed. */
    void refresh();

    [[nodiscard]] bool asksToReduceMotion() const noexcept { return reduceMotion; }

private:
    void received(bool asksToReduce);

    Callback callback;
    bool reduceMotion = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemMotion)
};

} // namespace daw::ui::desktop
