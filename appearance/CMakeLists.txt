# Theme values and animation settings shared by every user interface: colours, fonts,
# shapes, meter styles and the rules for animations. No GUI code (see CLAUDE.md,
# Architecture), so the mobile interface can reuse it.

juce_add_binary_data(daw_appearance_resources
    HEADER_NAME AppearanceResources.h
    NAMESPACE daw::appearance::resources
    SOURCES
        themes/neon-grid.json
        themes/acid-terminal.json
        themes/synth-chrome.json)

add_library(daw_appearance STATIC
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
