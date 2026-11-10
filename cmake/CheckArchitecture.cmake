# Checks the one-way dependency rules between modules by scanning #include
# directives. Registered as CTest tests (see tests/CMakeLists.txt); to run it
# by hand:
#
#   cmake -DDAW_SOURCE_DIR=<repository root> -P cmake/CheckArchitecture.cmake
#
# Rules:
#   - engine/, model/, controller/ and appearance/ never include GUI JUCE modules,
#     JuceHeader.h or daw/ui/
#   - engine/ and model/ never include each other, nor controller/
#   - appearance/ (theme data) includes no other module; engine/, model/ and
#     controller/ never include it
#   - services/ (build information, settings, log, updates, crash reports) includes no
#     other module; engine/, model/, controller/ and appearance/ never include it
#   - net/ (TLS links, frames, a reliable channel) includes no other module; engine/,
#     model/, appearance/ and services/ never include it
#   - coop/ (co-op sessions: protocol and synchronisation) uses model/ and net/ only;
#     engine/, model/, net/, appearance/ and services/ never include it

if(NOT DAW_SOURCE_DIR)
    message(FATAL_ERROR "DAW_SOURCE_DIR is not set")
endif()

# Include prefixes that bring in windowing, graphics or editor UI code.
# (juce_audio_processors_headless, plugin hosting without editors, stays allowed:
# the engine's plugin adapter and the controller's scanning use it; plugin editors
# and JUCE's full plugin formats belong to the UIs and the app.)
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

check_module(engine ${gui_includes} "daw/model/" "daw/controller/" "daw/appearance/"
    "daw/services/" "daw/coop/" "daw/net/")
check_module(model ${gui_includes} "daw/engine/" "daw/controller/" "daw/appearance/"
    "daw/services/" "daw/coop/" "daw/net/")
check_module(controller ${gui_includes} "daw/appearance/" "daw/services/")
check_module(appearance ${gui_includes} "daw/engine/" "daw/model/" "daw/controller/"
    "daw/services/" "daw/coop/" "daw/net/")
check_module(services ${gui_includes} "daw/engine/" "daw/model/" "daw/controller/"
    "daw/appearance/" "daw/coop/" "daw/net/")
check_module(net ${gui_includes} "daw/engine/" "daw/model/" "daw/controller/"
    "daw/appearance/" "daw/services/" "daw/coop/")
check_module(coop ${gui_includes} "daw/engine/" "daw/controller/" "daw/appearance/"
    "daw/services/")

if(violations)
    message(FATAL_ERROR "Module dependency rules violated (see CLAUDE.md, Architecture):${violations}")
endif()

message(STATUS "Module dependency rules respected")
