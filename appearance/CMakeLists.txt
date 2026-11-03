# Theme values and animation settings shared by every user interface: colours, fonts,
# shapes, meter styles and the rules for animations. No GUI code (see CLAUDE.md,
# Architecture), so the mobile interface can reuse it.
#
# The theme files and the fonts (SIL Open Font License, see docs/THIRD_PARTY.md) are
# embedded in the binary: nothing is downloaded at run time.

juce_add_binary_data(daw_appearance_resources
    HEADER_NAME AppearanceResources.h
    NAMESPACE daw::appearance::resources
    SOURCES
        themes/neon-grid.json
        themes/acid-terminal.json
        themes/synth-chrome.json
        fonts/ChakraPetch/ChakraPetch-Regular.ttf
        fonts/ChakraPetch/ChakraPetch-SemiBold.ttf
        fonts/JetBrainsMono/JetBrainsMono-Regular.ttf
        fonts/JetBrainsMono/JetBrainsMono-Bold.ttf
        fonts/SpaceGrotesk/SpaceGrotesk-Medium.ttf
        fonts/SpaceGrotesk/SpaceGrotesk-Bold.ttf
        fonts/IBMPlexMono/IBMPlexMono-Regular.ttf
        fonts/IBMPlexMono/IBMPlexMono-SemiBold.ttf
        fonts/Michroma/Michroma-Regular.ttf
        fonts/Rajdhani/Rajdhani-Medium.ttf
        fonts/Rajdhani/Rajdhani-SemiBold.ttf)

add_library(daw_appearance STATIC
    src/AnimationSettings.cpp
    src/AppearancePreferences.cpp
    src/BundledFonts.cpp
    src/Colour.cpp
    src/ColourMetrics.cpp
    src/ContrastRules.cpp
    src/Theme.cpp
    src/ThemeFile.cpp
    src/ThemeLibrary.cpp)
add_library(daw::appearance ALIAS daw_appearance)

target_include_directories(daw_appearance PUBLIC include PRIVATE src)
target_compile_features(daw_appearance PUBLIC cxx_std_20)
target_link_libraries(daw_appearance PUBLIC daw::juce PRIVATE daw_appearance_resources)
daw_enable_warnings(daw_appearance)
