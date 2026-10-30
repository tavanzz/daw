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

add_library(daw_juce STATIC)
add_library(daw::juce ALIAS daw_juce)

target_link_libraries(daw_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)

target_compile_definitions(daw_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        # Optional features we don't use; disabling them also removes their
        # Linux build dependencies (WebKit/GTK, libcurl, JACK, LADSPA).
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_JACK=0
        JUCE_PLUGINHOST_LADSPA=0
    INTERFACE
        $<TARGET_PROPERTY:daw_juce,COMPILE_DEFINITIONS>)

# SYSTEM: warnings inside JUCE headers don't count against our zero-warning rule.
target_include_directories(daw_juce SYSTEM
    INTERFACE
        $<TARGET_PROPERTY:daw_juce,INCLUDE_DIRECTORIES>)
