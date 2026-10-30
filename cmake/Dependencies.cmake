# Third-party dependencies, pinned to exact releases and verified by SHA-256.
#
# Archives are downloaded once into DAW_DEPENDENCY_CACHE_DIR and reused by every
# build directory; CI caches that directory between runs.

include(FetchContent)

set(DAW_DEPENDENCY_CACHE_DIR "${PROJECT_SOURCE_DIR}/.cache/deps"
    CACHE PATH "Where downloaded dependency archives are kept")

FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
    URL_HASH SHA256=e2bee25d5c776118aea550ad168a3dc9520c0669a79bd9f5a72b9f404ee1c40a
    DOWNLOAD_DIR "${DAW_DEPENDENCY_CACHE_DIR}"
    DOWNLOAD_NAME JUCE-9.0.4.tar.gz
    SYSTEM)

FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
    URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
    DOWNLOAD_DIR "${DAW_DEPENDENCY_CACHE_DIR}"
    DOWNLOAD_NAME Catch2-3.16.1.tar.gz
    SYSTEM)

FetchContent_MakeAvailable(JUCE)

if(DAW_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
