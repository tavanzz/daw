#include "daw/ui/desktop/Logo.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

Logo::Logo()
{
    // The product's name is set in one place (CMake); tests run without an application.
    auto* application = juce::JUCEApplicationBase::getInstance();
    text = application != nullptr ? application->getApplicationName() : juce::String("DAW");
    setInterceptsMouseClicks(false, false);
}

void Logo::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawLogo(g, getLocalBounds().toFloat(), text);
}

} // namespace daw::ui::desktop
