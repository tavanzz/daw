# Third-party dependencies, pinned to exact releases and verified by SHA-256.
#
# Archives are downloaded once into DAW_DEPENDENCY_CACHE_DIR and reused by every
# build directory; CI caches that directory between runs.

include(FetchContent)

set(DAW_DEPENDENCY_CACHE_DIR "${PROJECT_SOURCE_DIR}/.cache/deps"
    CACHE PATH "Where downloaded dependency archives are kept")

# One change to JUCE, macOS only (where the file is compiled): an output used without inputs
# goes into a stacked aggregate device, which has no input streams, so that playing to an
# interface that also has inputs never makes macOS ask for the microphone before a track
# needs an input. The patch file says why; cmake/patches/README.md what to do at every JUCE
# update. It is applied exactly or the configure stops, and checked after FetchContent.
set(DAW_JUCE_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/juce-coreaudio-output-only.patch")
if(APPLE)
    set(juce_patch PATCH_COMMAND "${CMAKE_COMMAND}" "-DPATCH_FILE=${DAW_JUCE_PATCH}"
        -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake")
else()
    set(juce_patch "")
endif()

FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
    URL_HASH SHA256=e2bee25d5c776118aea550ad168a3dc9520c0669a79bd9f5a72b9f404ee1c40a
    DOWNLOAD_DIR "${DAW_DEPENDENCY_CACHE_DIR}"
    DOWNLOAD_NAME JUCE-9.0.4.tar.gz
    ${juce_patch}
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

# TLS for co-op sessions (net/): Mbed TLS 4.1 LTS (supported until March 2029) with its
# TF-PSA-Crypto, Apache 2.0. Thread safe through our own mutexes (net/mbedtls/).
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
    URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
    DOWNLOAD_DIR "${DAW_DEPENDENCY_CACHE_DIR}"
    DOWNLOAD_NAME mbedtls-4.1.1.tar.bz2
    SYSTEM)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(DAW_MBEDTLS_CONFIG_DIR "${PROJECT_SOURCE_DIR}/net/mbedtls")
set(TF_PSA_CRYPTO_USER_CONFIG_FILE "${DAW_MBEDTLS_CONFIG_DIR}/crypto_user_config.h"
    CACHE FILEPATH "" FORCE)

FetchContent_MakeAvailable(JUCE Monocypher sentry mbedtls)

# The alternative threading header must be found by every part of Mbed TLS (it has
# several internal libraries) and by whoever includes its headers.
function(daw_targets_in directory result)
    get_property(found DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        daw_targets_in("${subdirectory}" more)
        list(APPEND found ${more})
    endforeach()
    set(${result} ${found} PARENT_SCOPE)
endfunction()
daw_targets_in("${mbedtls_SOURCE_DIR}" mbedtls_targets)
foreach(mbedtls_target IN LISTS mbedtls_targets)
    get_target_property(mbedtls_target_type ${mbedtls_target} TYPE)
    if(mbedtls_target_type MATCHES "LIBRARY$" AND NOT mbedtls_target_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${mbedtls_target} SYSTEM PUBLIC "${DAW_MBEDTLS_CONFIG_DIR}")
    endif()
endforeach()

# FetchContent runs the patch step only when it extracts the sources, and never on sources
# given with FETCHCONTENT_SOURCE_DIR_JUCE: this makes sure every macOS build has the patch.
if(APPLE)
    execute_process(
        COMMAND patch -p1 --reverse --dry-run --force --silent --fuzz=0
                --input "${DAW_JUCE_PATCH}"
        WORKING_DIRECTORY "${juce_SOURCE_DIR}"
        RESULT_VARIABLE juce_patched
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT juce_patched EQUAL 0)
        if(FETCHCONTENT_SOURCE_DIR_JUCE)
            set(juce_remedy "Apply it in that folder (patch -p1 < the patch).")
        else()
            string(CONCAT juce_remedy "Delete ${FETCHCONTENT_BASE_DIR}/juce-src and"
                " juce-subbuild next to it, then configure again: JUCE is extracted and"
                " patched anew.")
        endif()
        message(FATAL_ERROR
            "JUCE's sources in ${juce_SOURCE_DIR} lack ${DAW_JUCE_PATCH} (or have an older"
            " version of it), so the app would make macOS ask for the microphone at launch."
            " ${juce_remedy} cmake/patches/README.md says more.")
    endif()
endif()

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
