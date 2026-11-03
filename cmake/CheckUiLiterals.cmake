# Checks that the desktop UI's components hold no literal colours or fonts: they ask
# the theme's look and feel for roles (see CLAUDE.md, "Themed components"), so a
# theme change restyles everything. Registered as CTest tests (see
# tests/CMakeLists.txt); to run it by hand:
#
#   cmake -DDAW_SOURCE_DIR=<repository root> -P cmake/CheckUiLiterals.cmake
#
# Exempt: the look and feel files, which turn roles into colours and fonts.

# Scripts run with old policies unless told otherwise (IN_LIST needs CMP0057).
cmake_minimum_required(VERSION 3.25)

if(NOT DAW_SOURCE_DIR)
    message(FATAL_ERROR "DAW_SOURCE_DIR is not set")
endif()

file(GLOB_RECURSE sources LIST_DIRECTORIES false
    "${DAW_SOURCE_DIR}/ui/desktop/*.h"
    "${DAW_SOURCE_DIR}/ui/desktop/*.cpp"
    "${DAW_SOURCE_DIR}/ui/desktop/*.mm")

set(exempt "ThemedLookAndFeel" "ThemeLookAndFeels" "ThemeFonts")

# A colour made from numbers or a name, a font made by hand.
set(forbidden
    "juce::Colour[ \t]*[({]"
    "Colour::from[A-Za-z]+[ \t]*\\("
    "Colours::[A-Za-z]+"
    "FontOptions[ \t]*[({]"
    "juce::Font[ \t]*[({]"
    "Font::(bold|italic|underlined|plain)")

set(violations "")

if(NOT sources)
    string(APPEND violations "\n  ui/desktop/: no source files found, the scan is not checking anything")
endif()

foreach(source IN LISTS sources)
    get_filename_component(name "${source}" NAME_WE)
    if(name IN_LIST exempt)
        continue()
    endif()

    file(RELATIVE_PATH relative_source "${DAW_SOURCE_DIR}" "${source}")
    file(STRINGS "${source}" lines)
    set(number 0)

    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")

        foreach(pattern IN LISTS forbidden)
            string(REGEX MATCH "${pattern}" found "${line}")

            # Fully transparent colours carry no theme.
            if(found MATCHES "^Colours::transparent(Black|White)$")
                continue()
            endif()

            if(found)
                string(APPEND violations "\n  ${relative_source}:${number}: ${found}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR "Literal colours or fonts in UI components (use theme roles, see "
                        "CLAUDE.md):${violations}")
endif()

message(STATUS "No literal colours or fonts in UI components")
