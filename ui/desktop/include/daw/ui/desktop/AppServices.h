#pragma once

#include <juce_core/juce_core.h>

namespace daw::ui::desktop
{

/**
    What the app provides around the project for the Help menu (services/): where the log
    is. Absent parts switch their commands off (the UI tests run without them).
*/
struct AppServices
{
    juce::File logFolder; ///< Help > Open Log Folder
};

} // namespace daw::ui::desktop
