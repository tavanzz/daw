# Test plugins: small, deterministic plugins the tests host as they would any
# third-party plugin (see core/TestPluginCore.h). They are written against the
# formats' SDKs, not with JUCE, so they build in seconds and exercise JUCE's hosting
# the way real plugins do. They are built into ${DAW_TEST_PLUGINS_DIR}, never
# installed: plugins that behave go into VST3/, those that crash or hang when
# loaded into Faulty/VST3/ (only the out-of-process scanner may touch those).

set(DAW_TEST_PLUGINS_DIR "${CMAKE_BINARY_DIR}/test-plugins" CACHE INTERNAL "")
set(vst3_sdk "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK")

# The few SDK sources a plugin needs: FUnknown and the core interface ids.
add_library(daw_test_vst3_sdk STATIC
    "${vst3_sdk}/pluginterfaces/base/funknown.cpp"
    "${vst3_sdk}/pluginterfaces/base/coreiids.cpp")
target_include_directories(daw_test_vst3_sdk SYSTEM PUBLIC "${vst3_sdk}")
target_compile_features(daw_test_vst3_sdk PUBLIC cxx_std_20)
set_target_properties(daw_test_vst3_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(vst3_architecture "aarch64")
else()
    set(vst3_architecture "x86_64")
endif()

set(DAW_TEST_PLUGIN_TARGETS "" CACHE INTERNAL "")

# daw_add_test_vst3(<target> <kind> <name> <folder> <uid1> <uid2> <uid3> <uid4>)
function(daw_add_test_vst3 target kind name folder)
    add_library(${target} MODULE vst3/TestPluginVst3.cpp)
    target_link_libraries(${target} PRIVATE daw_test_vst3_sdk)
    target_compile_definitions(${target} PRIVATE
        DAW_TEST_PLUGIN_KIND=${kind}
        "DAW_TEST_PLUGIN_NAME=\"${name}\""
        DAW_TEST_PLUGIN_UID1=${ARGV4}
        DAW_TEST_PLUGIN_UID2=${ARGV5}
        DAW_TEST_PLUGIN_UID3=${ARGV6}
        DAW_TEST_PLUGIN_UID4=${ARGV7})
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "${name}"
        PREFIX "")
    daw_enable_warnings(${target})

    set(bundle "${DAW_TEST_PLUGINS_DIR}/${folder}/${name}.vst3")
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION vst3
            LIBRARY_OUTPUT_DIRECTORY "${DAW_TEST_PLUGINS_DIR}/${folder}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/vst3/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.daw.tests.${target}")
        target_link_libraries(${target} PRIVATE "-framework CoreFoundation")
    elseif(WIN32)
        set_target_properties(${target} PROPERTIES
            SUFFIX ".vst3"
            LIBRARY_OUTPUT_DIRECTORY "${bundle}/Contents/${vst3_architecture}-win")
    else()
        set_target_properties(${target} PROPERTIES
            SUFFIX ".so"
            LIBRARY_OUTPUT_DIRECTORY "${bundle}/Contents/${vst3_architecture}-linux")
    endif()

    set(DAW_TEST_PLUGIN_TARGETS ${DAW_TEST_PLUGIN_TARGETS} ${target} CACHE INTERNAL "")
endfunction()

daw_add_test_vst3(daw_test_vst3_gain gain "DAW Test Gain" VST3
    0x5D1A0001 0x4A2B4C3D 0x8E9F0A1B 0x2C3D4E01)
daw_add_test_vst3(daw_test_vst3_latency latency "DAW Test Latency" VST3
    0x5D1A0002 0x4A2B4C3D 0x8E9F0A1B 0x2C3D4E02)
daw_add_test_vst3(daw_test_vst3_synth synth "DAW Test Synth" VST3
    0x5D1A0003 0x4A2B4C3D 0x8E9F0A1B 0x2C3D4E03)
daw_add_test_vst3(daw_test_vst3_nan nan "DAW Test NaN" VST3
    0x5D1A0004 0x4A2B4C3D 0x8E9F0A1B 0x2C3D4E04)
daw_add_test_vst3(daw_test_vst3_crash crash "DAW Test Crash" Faulty/VST3
    0x5D1A0005 0x4A2B4C3D 0x8E9F0A1B 0x2C3D4E05)
daw_add_test_vst3(daw_test_vst3_hang hang "DAW Test Hang" Faulty/VST3
    0x5D1A0006 0x4A2B4C3D 0x8E9F0A1B 0x2C3D4E06)


# Audio Unit (v2) test plugins, macOS only: linked into the test binaries, which
# register them for their own process (au/TestAudioUnits.h); nothing is installed.
if(APPLE)
    set(au_sdk "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/AU/AudioUnitSDK")
    add_library(daw_test_audio_units STATIC
        au/TestAudioUnits.cpp
        "${au_sdk}/AUBase.cpp"
        "${au_sdk}/AUBuffer.cpp"
        "${au_sdk}/AUBufferAllocator.cpp"
        "${au_sdk}/AUEffectBase.cpp"
        "${au_sdk}/AUInputElement.cpp"
        "${au_sdk}/AUMIDIBase.cpp"
        "${au_sdk}/AUOutputElement.cpp"
        "${au_sdk}/AUPlugInDispatch.cpp"
        "${au_sdk}/AUScopeElement.cpp"
        "${au_sdk}/ComponentBase.cpp"
        "${au_sdk}/MusicDeviceBase.cpp")
    target_include_directories(daw_test_audio_units SYSTEM PRIVATE
        "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/AU")
    target_include_directories(daw_test_audio_units PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/au")
    target_compile_features(daw_test_audio_units PUBLIC cxx_std_20)
    target_link_libraries(daw_test_audio_units PUBLIC
        "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation"
        "-framework CoreMIDI")
    # The SDK's own sources are compiled as they come; only ours obeys our warnings.
    set_source_files_properties(au/TestAudioUnits.cpp PROPERTIES COMPILE_OPTIONS
        "-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion;-Wold-style-cast;-Werror")
endif()

# LV2 test plugins, where LV2 is hosted (Linux; elsewhere with DAW_HOST_LV2): a
# bundle each, with the library and its Turtle files.
if(DAW_HOST_LV2)
    set(lv2_sdk "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/lv2")

    # daw_add_test_lv2(<target> <kind> <name> <folder> <uri> <ttl template>)
    function(daw_add_test_lv2 target kind name folder uri template)
        set(bundle "${DAW_TEST_PLUGINS_DIR}/${folder}/${name}.lv2")
        add_library(${target} MODULE lv2/TestPluginLv2.cpp)
        target_include_directories(${target} SYSTEM PRIVATE "${lv2_sdk}")
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_compile_definitions(${target} PRIVATE
            DAW_TEST_PLUGIN_KIND=${kind} "DAW_TEST_PLUGIN_URI=\"${uri}\"")
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            PREFIX ""
            OUTPUT_NAME "${target}"
            LIBRARY_OUTPUT_DIRECTORY "${bundle}")
        daw_enable_warnings(${target})

        if(kind STREQUAL "synth")
            set(parameter "level")
            set(parameterName "Level")
            set(default "0.5")
        elseif(kind STREQUAL "nan")
            set(parameter "fail")
            set(parameterName "Fail")
            set(default "1.0")
        else()
            set(parameter "gain")
            set(parameterName "Gain")
            set(default "0.5")
        endif()
        set(binary "${target}${CMAKE_SHARED_MODULE_SUFFIX}")
        configure_file(lv2/manifest.ttl.in "${bundle}/manifest.ttl" @ONLY)
        configure_file(lv2/${template} "${bundle}/plugin.ttl" @ONLY)

        set(DAW_TEST_PLUGIN_TARGETS ${DAW_TEST_PLUGIN_TARGETS} ${target} CACHE INTERNAL "")
    endfunction()

    daw_add_test_lv2(daw_test_lv2_gain gain "DAW Test Gain" LV2 "urn:daw:test:gain" effect.ttl.in)
    daw_add_test_lv2(daw_test_lv2_latency latency "DAW Test Latency" LV2 "urn:daw:test:latency"
        effect.ttl.in)
    daw_add_test_lv2(daw_test_lv2_synth synth "DAW Test Synth" LV2 "urn:daw:test:synth"
        instrument.ttl.in)
    daw_add_test_lv2(daw_test_lv2_nan nan "DAW Test NaN" LV2 "urn:daw:test:nan" effect.ttl.in)
    daw_add_test_lv2(daw_test_lv2_crash crash "DAW Test Crash" Faulty/LV2 "urn:daw:test:crash"
        effect.ttl.in)
    daw_add_test_lv2(daw_test_lv2_hang hang "DAW Test Hang" Faulty/LV2 "urn:daw:test:hang"
        effect.ttl.in)
endif()

add_custom_target(daw_test_plugins DEPENDS ${DAW_TEST_PLUGIN_TARGETS})
