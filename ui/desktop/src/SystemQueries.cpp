#include "SystemQueries.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace daw::ui::desktop::detail
{

bool querySystemReducesMotion()
{
#if JUCE_WINDOWS
    // "Animation effects" in the accessibility settings.
    BOOL animationsOn = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsOn, 0) == FALSE)
        return false;
    return animationsOn == FALSE;
#elif JUCE_LINUX || JUCE_BSD
    // GNOME's switch (also read by other GTK desktops); without gsettings, no request.
    juce::ChildProcess gsettings;
    if (!gsettings.start("gsettings get org.gnome.desktop.interface enable-animations",
                         juce::ChildProcess::wantStdOut))
        return false;

    const auto output = gsettings.readAllProcessOutput().trim();
    return gsettings.waitForProcessToFinish(2000) && output == "false";
#else
    return false;
#endif
}

struct WindowOcclusion::Native
{
};

WindowOcclusion::WindowOcclusion(juce::Component&, std::function<void()>)
    : native(std::make_unique<Native>())
{
}

WindowOcclusion::~WindowOcclusion() = default;

bool WindowOcclusion::isCovered() const
{
    return false;
}

} // namespace daw::ui::desktop::detail
