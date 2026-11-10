include(Catch)

add_subdirectory(plugins)

add_executable(daw_tests
    support/Phase3Mixer.cpp
    support/RandomProjectEdits.cpp
    support/SimulatedNetwork.cpp
    support/RealtimeCheck.cpp
    support/RealtimeCheckTests.cpp
    support/SimulatedDevice.cpp
    support/TestMain.cpp
    support/TestAudioFiles.cpp
    support/TestPlugins.cpp
    appearance/AnimationSettingsTests.cpp
    appearance/BundledFontsTests.cpp
    appearance/ColourTests.cpp
    appearance/ContrastTests.cpp
    appearance/ThemeFileTests.cpp
    appearance/ThemeLibraryTests.cpp
    coop/MessageTests.cpp
    net/NetTests.cpp
    server/ServerTests.cpp
    controller/ArrangementBuilderTests.cpp
    controller/AutomationEditingTests.cpp
    controller/EffectSessionTests.cpp
    controller/MidiArrangementTests.cpp
    controller/MidiEditingTests.cpp
    controller/MidiFileTests.cpp
    controller/MidiRecordingSessionTests.cpp
    controller/NoteEditingTests.cpp
    controller/PluginCatalogueTests.cpp
    controller/PluginLoadsTests.cpp
    controller/PluginScannerTests.cpp
    controller/PluginSessionTests.cpp
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
    engine/DrumSamplerTests.cpp
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
    engine/PluginEffectTests.cpp
    engine/PluginHostingTests.cpp
    engine/RecorderTests.cpp
    engine/RecordingWriterTests.cpp
    engine/SamplerTests.cpp
    engine/ResamplerTests.cpp
    engine/ReverbTests.cpp
    engine/SourcePoolTests.cpp
    engine/SpscQueueTests.cpp
    engine/SynthTests.cpp
    engine/TransportTests.cpp
    engine/WavRepairTests.cpp
    engine/WaveformPeaksTests.cpp
    model/FormatVersionTests.cpp
    model/NetworkProtocolTests.cpp
    model/ProjectChangeTests.cpp
    model/ProjectFileTests.cpp
    model/ProjectTests.cpp
    model/TimelineTests.cpp
    services/AppLogTests.cpp
    services/BuildInfoTests.cpp
    services/NameMigrationTests.cpp
    services/CrashReportingTests.cpp
    services/UpdateCheckerTests.cpp
    services/UpdateInstallerTests.cpp
    services/UpdateManifestTests.cpp
    services/UserSettingsTests.cpp)

target_include_directories(daw_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(daw_tests PRIVATE daw::appearance daw::controller daw::coop daw::engine
    daw::model daw::net daw::server daw::services Catch2::Catch2)
daw_enable_warnings(daw_tests)
# The test plugins are built with the tests, which find them here.
add_dependencies(daw_tests daw_test_plugins)
if(APPLE)
    target_link_libraries(daw_tests PRIVATE daw_test_audio_units)
endif()
target_compile_definitions(daw_tests PRIVATE "DAW_TEST_PLUGINS_DIR=\"${DAW_TEST_PLUGINS_DIR}\"")
# The scanner program (app/scanner): the tests run it, as the app does, to scan plugins
# in a process of its own; they link none of the app's code.
add_dependencies(daw_tests daw_plugin_scanner)
target_compile_definitions(daw_tests PRIVATE
    "DAW_PLUGIN_SCANNER=\"$<TARGET_FILE:daw_plugin_scanner>\"")

# A program that crashes on purpose, for the crash report tests.
add_executable(daw_crash_probe support/CrashProbe.cpp)
target_link_libraries(daw_crash_probe PRIVATE daw::services daw::juce)
daw_enable_warnings(daw_crash_probe)
add_dependencies(daw_tests daw_crash_probe)
target_compile_definitions(daw_tests PRIVATE "DAW_CRASH_PROBE=\"$<TARGET_FILE:daw_crash_probe>\"")

# UI tests: the desktop interface's themes and windows, with JUCE's GUI running (on
# Linux CI, on a virtual display).
add_executable(daw_ui_tests
    support/TestAudioFiles.cpp
    support/UiTestMain.cpp
    support/WindowManagerAtoms.cpp
    ui/AlertsTests.cpp
    ui/AnimationClockTests.cpp
    ui/HelpPanelsTests.cpp
    ui/MidiUiTests.cpp
    ui/PanelsTests.cpp
    ui/PluginUiTests.cpp
    ui/PrerenderedImagesTests.cpp
    ui/ThemeManagerTests.cpp
    ui/ThemeSwitchTests.cpp
    ui/ThemedLookAndFeelTests.cpp)

target_include_directories(daw_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(daw_ui_tests PRIVATE daw::ui_desktop daw::appearance daw::controller
    daw::engine daw::model daw::juce Catch2::Catch2)
daw_enable_warnings(daw_ui_tests)
# The VST3 test plugins (support/TestPlugins.h, its inline parts only).
add_dependencies(daw_ui_tests daw_test_plugins)
target_compile_definitions(daw_ui_tests PRIVATE "DAW_TEST_PLUGINS_DIR=\"${DAW_TEST_PLUGINS_DIR}\"")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The virtual display in CI has no window manager (see WindowManagerAtoms.h).
    find_package(X11 REQUIRED)
    target_link_libraries(daw_ui_tests PRIVATE X11::X11)
endif()

# Not tests: measure how many tracks fit in an audio callback, how many stream
# from disk and record at once, what effects cost and what instruments cost per
# voice (see the sources).
add_executable(daw_bench bench/MixBenchmark.cpp)
target_link_libraries(daw_bench PRIVATE daw::engine)
daw_enable_warnings(daw_bench)

add_executable(daw_stream_bench bench/StreamingBenchmark.cpp)
target_link_libraries(daw_stream_bench PRIVATE daw::engine)
daw_enable_warnings(daw_stream_bench)

add_executable(daw_fx_bench bench/EffectBenchmark.cpp)
target_link_libraries(daw_fx_bench PRIVATE daw::engine)
daw_enable_warnings(daw_fx_bench)

add_executable(daw_instrument_bench bench/InstrumentBenchmark.cpp)
target_link_libraries(daw_instrument_bench PRIVATE daw::engine)
daw_enable_warnings(daw_instrument_bench)

# What hosting a plugin costs on top of the plugin (with the VST3 test plugins).
add_executable(daw_plugin_bench bench/PluginBenchmark.cpp)
target_include_directories(daw_plugin_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(daw_plugin_bench PRIVATE daw::engine daw::juce)
target_compile_definitions(daw_plugin_bench PRIVATE "DAW_TEST_PLUGINS_DIR=\"${DAW_TEST_PLUGINS_DIR}\"")
add_dependencies(daw_plugin_bench daw_test_plugins)
daw_enable_warnings(daw_plugin_bench)

# The interface's cost per theme and animation level, in a window of its own (an app,
# so that the system draws it as it draws the real one). See the source.
juce_add_gui_app(daw_ui_bench PRODUCT_NAME "DAW UI Bench" VERSION "${PROJECT_VERSION}")
daw_use_juce_settings(daw_ui_bench)
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

add_test(NAME architecture.detects_controller_include_in_services
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_controller_include_in_services PROPERTIES
    PASS_REGULAR_EXPRESSION "services/src/UsesController\\.cpp: <daw/controller/Session\\.h>")

add_test(NAME architecture.detects_services_include_in_controller
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_services_include_in_controller PROPERTIES
    PASS_REGULAR_EXPRESSION "controller/src/UsesServices\\.cpp: <daw/services/BuildInfo\\.h>")

add_test(NAME architecture.detects_controller_include_in_coop
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_controller_include_in_coop PROPERTIES
    PASS_REGULAR_EXPRESSION "coop/src/UsesController\\.cpp: <daw/controller/Session\\.h>")

add_test(NAME architecture.detects_model_include_in_net
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_model_include_in_net PROPERTIES
    PASS_REGULAR_EXPRESSION "net/src/UsesModel\\.cpp: <daw/model/Project\\.h>")

add_test(NAME architecture.detects_engine_include_in_server
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_engine_include_in_server PROPERTIES
    PASS_REGULAR_EXPRESSION "server/src/UsesEngine\\.cpp: <daw/engine/Engine\\.h>")

add_test(NAME architecture.detects_server_include_in_controller
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_server_include_in_controller PROPERTIES
    PASS_REGULAR_EXPRESSION "controller/src/UsesServer\\.cpp: <daw/server/Server\\.h>")

add_test(NAME architecture.detects_coop_include_in_engine
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_coop_include_in_engine PROPERTIES
    PASS_REGULAR_EXPRESSION "engine/src/UsesCoop\\.cpp: <daw/coop/Messages\\.h>")

add_test(NAME architecture.detects_plugin_editors_in_engine
    COMMAND "${CMAKE_COMMAND}" "-DDAW_SOURCE_DIR=${violating_tree}" -P "${check_architecture}")
set_tests_properties(architecture.detects_plugin_editors_in_engine PROPERTIES
    PASS_REGULAR_EXPRESSION
    "engine/src/UsesPluginEditors\\.cpp: <juce_audio_processors/juce_audio_processors\\.h>"
    FAIL_REGULAR_EXPRESSION "HostsPlugins")

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
