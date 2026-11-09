#include "daw/ui/desktop/Logo.h"

#include "daw/services/BuildInfo.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

Logo::Logo()
{
    // The product's name as logos show it, set in one place (CMake).
    text = services::buildInfo().logoText;
    setInterceptsMouseClicks(false, false);
}

void Logo::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawLogo(g, getLocalBounds().toFloat(), text, glitch.getTime(),
                                           glitch.isActive());
}

} // namespace daw::ui::desktop
