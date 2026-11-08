cmake_minimum_required(VERSION 3.25)

# Must be set before project() so that it applies to every target, JUCE included.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(daw
    VERSION 0.7.0
    DESCRIPTION "Cross-platform digital audio workstation"
    LANGUAGES C CXX)

# User-visible identity. "DAW" is a working title: change these here only, nothing else
# in the code depends on their values. The bundle id stays once friends have the app
# (macOS ties permissions such as the microphone's to it).
set(DAW_PRODUCT_NAME "DAW" CACHE STRING "User-visible product name")
set(DAW_COMPANY_NAME "tavanzz" CACHE STRING "Publisher shown in app metadata and installers")
set(DAW_BUNDLE_ID "io.github.tavanzz.daw" CACHE STRING "Bundle identifier")
set(DAW_REPOSITORY_URL "https://github.com/tavanzz/daw" CACHE STRING
    "GitHub repository: links in the app, and where updates come from")

# A release tag vX.Y.Z-rc.N builds version X.Y.Z (above) with the suffix "-rc.N"; the
# release workflow passes it. The full version is a semantic version.
set(DAW_VERSION_SUFFIX "" CACHE STRING "Pre-release suffix of the version, e.g. -rc.1")
if(NOT DAW_VERSION_SUFFIX MATCHES "^(-[0-9A-Za-z]+(\\.[0-9A-Za-z]+)*)?$")
    message(FATAL_ERROR "DAW_VERSION_SUFFIX must look like -rc.1, not '${DAW_VERSION_SUFFIX}'")
endif()
set(DAW_VERSION "${PROJECT_VERSION}${DAW_VERSION_SUFFIX}")

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

# The C and C++ runtimes go into each program on Windows, so the installed app needs no
# Visual C++ redistributable. Set before any target exists, JUCE's and the tests' too, so
# every library agrees.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(Warnings)
include(Dependencies)
include(JuceLibrary)

add_subdirectory(engine)
add_subdirectory(model)
add_subdirectory(controller)
add_subdirectory(appearance)
add_subdirectory(services)
add_subdirectory(ui/desktop)
add_subdirectory(app)

if(DAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
