# Controller: maps the project (model) onto the engine. Loading, arrangement
# building, editing commands with undo, timeline view math and export, shared by
# every user interface. Never depends on any UI module (see CLAUDE.md, Architecture).

add_library(daw_controller STATIC
    src/ArrangementBuilder.cpp
    src/ClipEditing.cpp
    src/ExportJob.cpp
    src/Session.cpp
    src/TimelineViewport.cpp)
add_library(daw::controller ALIAS daw_controller)

target_include_directories(daw_controller PUBLIC include PRIVATE src)
target_compile_features(daw_controller PUBLIC cxx_std_20)
target_link_libraries(daw_controller PUBLIC daw::engine daw::model daw::juce)
daw_enable_warnings(daw_controller)
