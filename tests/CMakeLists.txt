include(Catch)

add_executable(daw_tests
    support/Phase3Mixer.cpp
    support/RealtimeCheck.cpp
    support/RealtimeCheckTests.cpp
    support/SimulatedDevice.cpp
    support/TestMain.cpp
    support/TestAudioFiles.cpp
    appearance/AnimationSettingsTests.cpp
    appearance/BundledFontsTests.cpp
    appearance/ColourTests.cpp
    appearance/ContrastTests.cpp
    appearance/ThemeFileTests.cpp
    appearance/ThemeLibraryTests.cpp
    controller/ArrangementBuilderTests.cpp
    controller/AutomationEditingTests.cpp
    controller/EffectSessionTests.cpp
    controller/MidiEditingTests.cpp
    controller/NoteEditingTests.cpp
    controller/MetronomeTests.cpp
    controller/RecordingTests.cpp
    controller/SessionTests.cpp
    controller/StreamingTests.cpp
    controller/TimelineTests.cpp
    engine/AudioDeviceHostTests.cpp
    engine/AudioInputTests.cpp
    engine/AutomationTests.cpp
    engine/AudioFileLoaderTests.cpp
    engine/BounceTests.cpp
    engine/BusTests.cpp
    engine/DelayTests.cpp
    engine/DiskStreamerTests.cpp
    engine/DynamicsTests.cpp
    engine/EffectChainTests.cpp
    engine/EffectHostTests.cpp
    engine/EngineTests.cpp
    engine/EqualiserTests.cpp
    engine/HangingNoteTests.cpp
    engine/InstrumentHostTests.cpp
    engine/LatencyCompensationTests.cpp
    engine/LatencyProbeTests.cpp
    engine/MeterTests.cpp
    engine/MetronomeTests.cpp
    engine/MidiEventTests.cpp
    engine/MidiInputTests.cpp
    engine/MidiPlaybackTests.cpp
    engine/MidiRecordingTests.cpp
    engine/MixerTests.cpp
    engine/MonitoringTests.cpp
    engine/RecorderTests.cpp
    engine/RecordingWriterTests.cpp
    engine/ResamplerTests.cpp
    engine/ReverbTests.cpp
    engine/SourcePoolTests.cpp
    engine/SpscQueueTests.cpp
    engine/TransportTests.cpp
    engine/WavRepairTests.cpp
    engine/WaveformPeaksTests.cpp
    model/FormatVersionTests.cpp
    model/ProjectFileTests.cpp
    model/ProjectTests.cpp
    model/TimelineTests.cpp)

target_include_directories(daw_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(daw_tests PRIVATE daw::appearance daw::controller daw::engine daw::model
    Catch2::Catch2)
daw_enable_warnings(daw_tests)

# UI tests: the desktop interface's themes and windows, with JUCE's GUI running (on
# Linux CI, on a virtual display).
add_executable(daw_ui_tests
    support/TestAudioFiles.cpp
    support/UiTestMain.cpp
    support/WindowManagerAtoms.cpp
    ui/AlertsTests.cpp
    ui/AnimationClockTests.cpp
    ui/PanelsTests.cpp
    ui/PrerenderedImagesTests.cpp
    ui/ThemeManagerTests.cpp
    ui/ThemeSwitchTests.cpp
    ui/ThemedLookAndFeelTests.cpp)

target_include_directories(daw_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(daw_ui_tests PRIVATE daw::ui_desktop daw::appearance daw::controller
    daw::engine daw::model daw::juce Catch2::Catch2)
daw_enable_warnings(daw_ui_tests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The virtual display in CI has no window manager (see WindowManagerAtoms.h).
    find_package(X11 REQUIRED)
    target_link_libraries(daw_ui_tests PRIVATE X11::X11)
endif()

# Not tests: measure how many tracks fit in an audio callback, how many stream
# from disk and record at once, and what effects cost (see the sources).
add_executable(daw_bench bench/MixBenchmark.cpp)
target_link_libraries(daw_bench PRIVATE daw::engine)
daw_enable_warnings(daw_bench)

add_executable(daw_stream_bench bench/StreamingBenchmark.cpp)
target_link_libraries(daw_stream_bench PRIVATE daw::engine)
daw_enable_warnings(daw_stream_bench)

add_executable(daw_fx_bench bench/EffectBenchmark.cpp)
target_link_libraries(daw_fx_bench PRIVATE daw::engine)
daw_enable_warnings(daw_fx_bench)

# The interface's cost per theme and animation level, in a window of its own (an app,
# so that the system draws it as it draws the real one). See the source.
juce_add_gui_app(daw_ui_bench PRODUCT_NAME "DAW UI Bench" VERSION "${PROJECT_VERSION}")
target_sources(daw_ui_bench PRIVATE bench/UiBenchmark.cpp)
target_link_libraries(daw_ui_bench PRIVATE daw::ui_desktop daw::appearance daw::controller
    daw::engine daw::model daw::juce)
daw_enable_warnings(daw_ui_bench)

# PRE_TEST: tests are listed when CTest runs, not at build time, so building
# never needs to execute the test binary.
catch_discover_tests(daw_tests DISCOVERY_MODE PRE_TEST)
catch_discover_tests(daw_ui_tests DISCOVERY_MODE PRE_TEST)

if(DAW_ENABLE_RTSAN)
    # Proves the sanitizer is really active: this hidden test calls malloc from
    # audio-thread code and must be stopped with RTSan's report.
    add_test(NAME rtsan.self_test COMMAND daw_tests "[.rtsan-self-test]")
    set_tests_properties(rtsan.self_test PROPERTIES
        PASS_REGULAR_EXPRESSION "RealtimeSanitizer")
endif()

# Module dependency rules (see cmake/CheckArchitecture.cmake).
set(check_architecture "${PROJECT_SOURCE_DIR}/cmake/CheckArchitecture.cmake")

add_test(NAME architecture.dependency_rules
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${check_architecture}")

# The check must catch real violations, not just pass: run it on a small tree
# that breaks each rule and expect each violation to be reported.
set(violating_tree "${CMAKE_CURRENT_SOURCE_DIR}/architecture/violating_tree")

add_test(NAME architecture.detects_gui_include_in_engine
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_gui_include_in_engine PROPERTIES
    PASS_REGULAR_EXPRESSION "engine/src/UsesGui\\.cpp: <juce_gui_basics/juce_gui_basics\\.h>")

add_test(NAME architecture.detects_engine_include_in_model
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_engine_include_in_model PROPERTIES
    PASS_REGULAR_EXPRESSION "model/src/UsesEngine\\.cpp: <daw/engine/Engine\\.h>")

add_test(NAME architecture.detects_gui_include_in_controller
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_gui_include_in_controller PROPERTIES
    PASS_REGULAR_EXPRESSION "controller/src/UsesGui\\.cpp: <juce_gui_basics/juce_gui_basics\\.h>")

add_test(NAME architecture.detects_gui_include_in_appearance
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_gui_include_in_appearance PROPERTIES
    PASS_REGULAR_EXPRESSION "appearance/src/UsesGui\\.cpp: <juce_graphics/juce_graphics\\.h>")

add_test(NAME architecture.detects_appearance_include_in_engine
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_appearance_include_in_engine PROPERTIES
    PASS_REGULAR_EXPRESSION "engine/src/UsesAppearance\\.cpp: <daw/appearance/Theme\\.h>")

# No literal colours or fonts in the desktop UI's components (cmake/CheckUiLiterals.cmake).
set(check_ui_literals "${PROJECT_SOURCE_DIR}/cmake/CheckUiLiterals.cmake")

add_test(NAME architecture.ui_literals
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${check_ui_literals}")

add_test(NAME architecture.detects_ui_literals
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/architecture/violating_ui"
            -P "${check_ui_literals}")
set_tests_properties(architecture.detects_ui_literals PROPERTIES
    PASS_REGULAR_EXPRESSION
    "LiteralLook\\.cpp:5: juce::Colour\\(.*LiteralLook\\.cpp:6: FontOptions\\(")
