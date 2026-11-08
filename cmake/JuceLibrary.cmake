# Compiles the JUCE modules once, into one internal static library (daw::juce),
# with JUCE's own recommended flags. Project targets link daw::juce and include
# module headers directly, e.g. <juce_audio_basics/juce_audio_basics.h>.
#
# Linking JUCE modules PRIVATE keeps their sources out of the targets that link
# daw::juce; their definitions and include directories are re-exported below so
# that every consumer sees exactly the configuration JUCE was compiled with.
#
# Every module is visible to every target linking daw::juce, so "engine and model
# never use GUI modules" is enforced by the architecture tests
# (cmake/CheckArchitecture.cmake), not by the linker.

# LV2 is hosted on Linux, where its plugins live; elsewhere it can be switched on to
# test the LV2 code paths on a development machine.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(lv2_by_default ON)
else()
    set(lv2_by_default OFF)
endif()
option(DAW_HOST_LV2 "Host LV2 plugins" ${lv2_by_default})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Only curl's header is needed to build: the library is opened at run time.
    find_path(DAW_CURL_INCLUDE_DIR curl/curl.h)
    if(NOT DAW_CURL_INCLUDE_DIR)
        message(FATAL_ERROR "curl/curl.h not found: install libcurl's development package "
                            "(Debian and Ubuntu: libcurl4-openssl-dev)")
    endif()
endif()

add_library(daw_juce STATIC)
add_library(daw::juce ALIAS daw_juce)

target_link_libraries(daw_juce
    PRIVATE
        juce::juce_core
        juce::juce_cryptography
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_processors_headless
        juce::juce_recommended_config_flags)

target_compile_definitions(daw_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        # Optional features we don't use; disabling them also removes their
        # Linux build dependencies (WebKit/GTK, libcurl, JACK, LADSPA).
        JUCE_WEB_BROWSER=0
        # HTTPS (updates, crash reports): macOS and Windows have their own; on Linux
        # JUCE uses libcurl, loaded only when first needed, so the app still starts on
        # a system without it (the network then fails, nothing else).
        JUCE_USE_CURL=$<IF:$<PLATFORM_ID:Linux>,1,0>
        JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
        JUCE_JACK=0
        JUCE_PLUGINHOST_LADSPA=0
        # Plugin hosting (Phase 7): VST3 everywhere, AU on macOS, LV2 on Linux. No
        # VST2 (its SDK cannot be distributed) and no ARA.
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=$<IF:$<PLATFORM_ID:Darwin>,1,0>
        JUCE_PLUGINHOST_LV2=$<BOOL:${DAW_HOST_LV2}>
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_ARA=0
    INTERFACE
        $<TARGET_PROPERTY:daw_juce,COMPILE_DEFINITIONS>)

# SYSTEM: warnings inside JUCE headers don't count against our zero-warning rule.
target_include_directories(daw_juce SYSTEM
    INTERFACE
        $<TARGET_PROPERTY:daw_juce,INCLUDE_DIRECTORIES>)
