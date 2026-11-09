# Services the app needs around the audio: build information, user settings, the log,
# updates and crash reports. No GUI code and no other module of ours (see CLAUDE.md,
# Architecture), so the desktop and the mobile interfaces can share it.

# The commit the build comes from, refreshed at every build (only a change rewrites the
# header, so nothing recompiles otherwise). Without git (a source archive): "unknown".
set(build_commit_header "${CMAKE_CURRENT_BINARY_DIR}/generated/daw/services/BuildCommit.h")
add_custom_target(daw_services_commit
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT=${build_commit_header}" -P "${PROJECT_SOURCE_DIR}/cmake/BuildCommit.cmake"
    BYPRODUCTS "${build_commit_header}"
    COMMENT "Reading the commit the build comes from"
    VERBATIM)

add_library(daw_services STATIC
    src/AppLog.cpp
    src/BuildInfo.cpp
    src/CrashReporting.cpp
    src/Ed25519.cpp
    src/Http.cpp
    src/NameMigration.cpp
    src/SemanticVersion.cpp
    src/UpdateChecker.cpp
    src/UpdateInstaller.cpp
    src/UpdateManifest.cpp
    src/UserSettings.cpp)
add_library(daw::services ALIAS daw_services)
add_dependencies(daw_services daw_services_commit)

target_include_directories(daw_services
    PUBLIC include
    PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(daw_services PUBLIC cxx_std_20)
target_link_libraries(daw_services PUBLIC daw::juce PRIVATE daw_monocypher sentry::sentry)
target_compile_definitions(daw_services PRIVATE
    "DAW_PRODUCT_NAME=\"${DAW_PRODUCT_NAME}\""
    "DAW_VERSION=\"${DAW_VERSION}\""
    "DAW_BUNDLE_ID=\"${DAW_BUNDLE_ID}\""
    "DAW_LOGO_TEXT=\"${DAW_LOGO_TEXT}\""
    "DAW_PREVIOUS_PRODUCT_NAME=\"${DAW_PREVIOUS_PRODUCT_NAME}\""
    "DAW_UPDATE_ID=\"${DAW_UPDATE_ID}\""
    "DAW_REPOSITORY_URL=\"${DAW_REPOSITORY_URL}\""
    "DAW_BUILD_TYPE=\"$<CONFIG>\""
    "DAW_UPDATE_PUBLIC_KEY=\"${DAW_UPDATE_PUBLIC_KEY}\""
    "DAW_SENTRY_DSN=\"${DAW_SENTRY_DSN}\"")
daw_enable_warnings(daw_services)
