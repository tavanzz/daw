# Desktop user interface (mouse and keyboard).

add_library(daw_ui_desktop STATIC
    src/ArrangeView.cpp
    src/ExportDialog.cpp
    src/LevelMeter.cpp
    src/MainComponent.cpp
    src/MainWindow.cpp
    src/MixerView.cpp
    src/TimelineRuler.cpp
    src/TrackHeaderList.cpp
    src/TrackInputControls.cpp
    src/TrackLanes.cpp
    src/TransportBar.cpp)
add_library(daw::ui_desktop ALIAS daw_ui_desktop)

target_include_directories(daw_ui_desktop PUBLIC include PRIVATE src)
target_compile_features(daw_ui_desktop PUBLIC cxx_std_20)
target_link_libraries(daw_ui_desktop PUBLIC daw::controller daw::engine daw::model daw::juce)
daw_enable_warnings(daw_ui_desktop)
