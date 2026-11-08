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

# Ed25519 signatures of updates (services/). Dual-licensed CC0 or BSD 2-clause; it has
# no CMake project, so its two source files become a library here.
FetchContent_Declare(Monocypher
    URL https://monocypher.org/download/monocypher-4.0.2.tar.gz
    URL_HASH SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c
    DOWNLOAD_DIR "${DAW_DEPENDENCY_CACHE_DIR}"
    DOWNLOAD_NAME monocypher-4.0.2.tar.gz)

# Crash reports (services/CrashReporting): sentry-native, MIT, with its in-process
# backend (a stack walk at the crash, no memory dump) and no transport of its own (the
# app sends reports itself, only with the user's consent). Its vendored parts used here
# (mpack, jsmn, stb_sprintf, libunwind on Linux) are MIT or public domain.
FetchContent_Declare(sentry
    URL https://github.com/getsentry/sentry-native/releases/download/0.17.4/sentry-native.zip
    URL_HASH SHA256=804811557b5d42ddd51cb1241bf851964d75547a4eb7b5ad97b73a4de965ab25
    DOWNLOAD_DIR "${DAW_DEPENDENCY_CACHE_DIR}"
    DOWNLOAD_NAME sentry-native-0.17.4.zip
    SYSTEM)
set(SENTRY_BACKEND "inproc" CACHE STRING "" FORCE)
set(SENTRY_TRANSPORT "none" CACHE STRING "" FORCE)
set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_RUNTIMESTATIC ON CACHE BOOL "" FORCE)
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(JUCE Monocypher sentry)

add_library(daw_monocypher STATIC
    "${monocypher_SOURCE_DIR}/src/monocypher.c"
    "${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(daw_monocypher SYSTEM PUBLIC
    "${monocypher_SOURCE_DIR}/src"
    "${monocypher_SOURCE_DIR}/src/optional")

if(DAW_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
