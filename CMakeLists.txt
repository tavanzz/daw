cmake_minimum_required(VERSION 3.25)

# Must be set before project() so that it applies to every target, JUCE included.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(daw
    VERSION 0.1.0
    DESCRIPTION "Cross-platform digital audio workstation"
    LANGUAGES C CXX)

# User-visible identity. Placeholders until the product has a name: change them
# here only, nothing else in the code depends on their values.
set(DAW_PRODUCT_NAME "DAW" CACHE STRING "User-visible product name")
set(DAW_COMPANY_NAME "DAW" CACHE STRING "Company name shown in app metadata")
set(DAW_BUNDLE_ID "com.example.daw" CACHE STRING "Bundle identifier; set a real one before distributing")

option(DAW_BUILD_TESTS "Build the unit tests" ON)
option(DAW_ENABLE_RTSAN "Build with Clang's RealtimeSanitizer (Clang 20 or newer)" OFF)

if(DAW_ENABLE_RTSAN)
    # Applied to everything, JUCE included, before any target is created.
    add_compile_options(-fsanitize=realtime)
    add_link_options(-fsanitize=realtime)
endif()

# Same language standard for JUCE and for our code, so they agree on the ABI.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(Warnings)
include(Dependencies)
include(JuceLibrary)

add_subdirectory(engine)
add_subdirectory(model)
add_subdirectory(controller)
add_subdirectory(ui/desktop)
add_subdirectory(app)

if(DAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
