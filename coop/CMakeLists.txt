# Co-op sessions: the protocol, the synchronisation of a participant's copy, the logic of
# the server's sessions. No UI, no audio (see CLAUDE.md, Architecture).

add_library(daw_coop STATIC
    src/Client.cpp
    src/Messages.cpp
    src/Wire.cpp)
add_library(daw::coop ALIAS daw_coop)

target_include_directories(daw_coop PUBLIC include PRIVATE src)
target_compile_features(daw_coop PUBLIC cxx_std_20)
target_link_libraries(daw_coop PUBLIC daw::model daw::net daw::juce)
daw_enable_warnings(daw_coop)
