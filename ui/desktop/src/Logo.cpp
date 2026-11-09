#include "daw/ui/desktop/Logo.h"

#include "daw/services/BuildInfo.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <cmath>

namespace daw::ui::desktop
{

Logo::Logo()
{
    // The product's name as logos show it, set in one place (CMake).
    text = services::buildInfo().logoText;
    setInterceptsMouseClicks(false, false);
}

int Logo::getPreferredWidth(int height)
{
    return static_cast<int>(std::ceil(ThemedLookAndFeel::get(*this).logoWidth(text, static_cast<float>(height))));
}

void Logo::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawLogo(g, getLocalBounds().toFloat(), text, glitch.getTime(),
                                           glitch.isActive());
}

} // namespace daw::ui::desktop
