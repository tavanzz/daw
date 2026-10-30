# Project state (tracks, clips, parameters) and versioned project files.
# Never depends on any UI module, nor on the engine (see CLAUDE.md, Architecture).

add_library(daw_model STATIC
    src/Clip.cpp
    src/FormatVersion.cpp
    src/Grid.cpp
    src/Project.cpp
    src/ProjectFile.cpp
    src/TempoMap.cpp
    src/TimelineTime.cpp
    src/Track.cpp)
add_library(daw::model ALIAS daw_model)

target_include_directories(daw_model PUBLIC include PRIVATE src)
target_compile_features(daw_model PUBLIC cxx_std_20)
target_link_libraries(daw_model PUBLIC daw::juce)
daw_enable_warnings(daw_model)
