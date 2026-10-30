include(Catch)

add_executable(daw_tests
    engine/EngineTests.cpp
    model/FormatVersionTests.cpp)

target_link_libraries(daw_tests PRIVATE daw::engine daw::model Catch2::Catch2WithMain)
daw_enable_warnings(daw_tests)

# PRE_TEST: tests are listed when CTest runs, not at build time, so building
# never needs to execute the test binary.
catch_discover_tests(daw_tests DISCOVERY_MODE PRE_TEST)

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
