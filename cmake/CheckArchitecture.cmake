# Checks the one-way dependency rules between modules by scanning #include
# directives. Registered as CTest tests (see tests/CMakeLists.txt); to run it
# by hand:
#
#   cmake -DDAW_SOURCE_DIR=<repository root> -P cmake/CheckArchitecture.cmake
#
# Rules:
#   - engine/, model/ and controller/ never include GUI JUCE modules, JuceHeader.h
#     or daw/ui/
#   - engine/ and model/ never include each other, nor controller/

if(NOT DAW_SOURCE_DIR)
    message(FATAL_ERROR "DAW_SOURCE_DIR is not set")
endif()

# Include prefixes that bring in windowing, graphics or editor UI code.
# (juce_audio_processors_headless, which plugin hosting will use, stays allowed.)
set(gui_includes
    "juce_graphics/"
    "juce_gui_basics/"
    "juce_gui_extra/"
    "juce_opengl/"
    "juce_audio_utils/"
    "juce_audio_processors/"
    "JuceHeader.h"
    "daw/ui/")

set(violations "")

# check_module(<module directory> <forbidden include prefix>...)
function(check_module module_dir)
    file(GLOB_RECURSE sources LIST_DIRECTORIES false
        "${DAW_SOURCE_DIR}/${module_dir}/*.h"
        "${DAW_SOURCE_DIR}/${module_dir}/*.hpp"
        "${DAW_SOURCE_DIR}/${module_dir}/*.cpp"
        "${DAW_SOURCE_DIR}/${module_dir}/*.mm")

    set(found "${violations}")

    if(NOT sources)
        string(APPEND found "\n  ${module_dir}/: no source files found, the scan is not checking anything")
    endif()

    foreach(source IN LISTS sources)
        file(STRINGS "${source}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
        file(RELATIVE_PATH relative_source "${DAW_SOURCE_DIR}" "${source}")

        foreach(line IN LISTS include_lines)
            string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]*)[>\"].*" "\\1" header "${line}")

            foreach(forbidden IN LISTS ARGN)
                string(FIND "${header}" "${forbidden}" position)
                if(position EQUAL 0)
                    string(APPEND found "\n  ${relative_source}: <${header}>")
                endif()
            endforeach()
        endforeach()
    endforeach()

    set(violations "${found}" PARENT_SCOPE)
endfunction()

check_module(engine ${gui_includes} "daw/model/" "daw/controller/")
check_module(model ${gui_includes} "daw/engine/" "daw/controller/")
check_module(controller ${gui_includes})

if(violations)
    message(FATAL_ERROR "Module dependency rules violated (see CLAUDE.md, Architecture):${violations}")
endif()

message(STATUS "Module dependency rules respected")
