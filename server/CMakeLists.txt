# The co-op server: sessions, their one order of transactions checked against the shared
# project (the model), locks, invites, presence, storage; and its program,
# micinstation-server (docs/SERVER.md). Uses model, net and coop, never the engine, the
# controller or a UI.

add_library(daw_server STATIC
    src/FileStore.cpp
    src/Server.cpp
    src/SessionStore.cpp)
add_library(daw::server ALIAS daw_server)

target_include_directories(daw_server PUBLIC include PRIVATE src)
target_compile_features(daw_server PUBLIC cxx_std_20)
target_link_libraries(daw_server PUBLIC daw::coop daw::net daw::model daw::juce)
daw_enable_warnings(daw_server)

add_executable(daw_server_program program/ServerMain.cpp)
set_target_properties(daw_server_program PROPERTIES OUTPUT_NAME micinstation-server)
target_link_libraries(daw_server_program PRIVATE daw::server daw::coop daw::net daw::model daw::juce)
daw_enable_warnings(daw_server_program)
