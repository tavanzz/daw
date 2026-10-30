include(Catch)

add_executable(daw_tests
    support/RealtimeCheck.cpp
    support/RealtimeCheckTests.cpp
    support/TestAudioFiles.cpp
    engine/AudioFileLoaderTests.cpp
    engine/EngineTests.cpp
    engine/MeterTests.cpp
    engine/ResamplerTests.cpp
    engine/SpscQueueTests.cpp
    engine/TransportTests.cpp
    engine/WaveformPeaksTests.cpp
    model/FormatVersionTests.cpp
    model/ProjectFileTests.cpp
    model/ProjectTests.cpp)

target_include_directories(daw_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(daw_tests PRIVATE daw::engine daw::model Catch2::Catch2WithMain)
daw_enable_warnings(daw_tests)

# PRE_TEST: tests are listed when CTest runs, not at build time, so building
# never needs to execute the test binary.
catch_discover_tests(daw_tests DISCOVERY_MODE PRE_TEST)

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
