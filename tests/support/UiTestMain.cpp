// Entry point of the UI tests: runs Catch2 with JUCE's GUI set up as in the app
// (message manager, desktop, fonts). On Linux, CI runs them on a virtual display.

#include "support/WindowManagerAtoms.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    const daw::test::WindowManagerAtoms windowManagerAtoms; // before JUCE opens any window
    const juce::ScopedJuceInitialiser_GUI gui;
    return Catch::Session().run(argc, argv);
}
