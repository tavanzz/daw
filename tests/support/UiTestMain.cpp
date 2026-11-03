// Entry point of the UI tests: runs Catch2 with JUCE's GUI set up as in the app
// (message manager, desktop, fonts). On Linux, CI runs them on a virtual display.

#include <catch2/catch_session.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;
    return Catch::Session().run(argc, argv);
}
