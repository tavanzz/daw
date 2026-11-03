#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

// Questions only the operating system can answer, one implementation per platform
// (SystemQueries_apple.mm on macOS, SystemQueries.cpp elsewhere).

namespace daw::ui::desktop::detail
{

/** True if the system asks apps to reduce motion. May block briefly (on Linux it runs
    gsettings): not on the message thread there. */
[[nodiscard]] bool querySystemReducesMotion();

/** Tells when a window becomes completely covered (by other windows, or on another
    Space) or visible again. Only macOS reports it; elsewhere a window is never
    covered. */
class WindowOcclusion final
{
public:
    WindowOcclusion(juce::Component& window, std::function<void()> onChange);
    ~WindowOcclusion();

    [[nodiscard]] bool isCovered() const;

private:
    struct Native;
    std::unique_ptr<Native> native;
};

} // namespace daw::ui::desktop::detail
