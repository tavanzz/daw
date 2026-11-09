# Development and release tools built with the project.

# Keys and manifests of signed updates (services/UpdateManifest.h): the release workflow
# signs each release with it; see docs/RELEASING.md for making the key pair.
add_executable(daw_update_tool update-tool/UpdateToolMain.cpp)
target_link_libraries(daw_update_tool PRIVATE daw::services daw::juce)
if(APPLE)
    target_link_libraries(daw_update_tool PRIVATE "-framework Security")
endif()
daw_enable_warnings(daw_update_tool)
