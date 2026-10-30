# Desktop user interface (mouse and keyboard).

add_library(daw_ui_desktop STATIC
    src/LevelMeter.cpp
    src/MainComponent.cpp
    src/MainWindow.cpp
    src/TransportBar.cpp
    src/WaveformView.cpp)
add_library(daw::ui_desktop ALIAS daw_ui_desktop)

target_include_directories(daw_ui_desktop PUBLIC include PRIVATE src)
target_compile_features(daw_ui_desktop PUBLIC cxx_std_20)
target_link_libraries(daw_ui_desktop PUBLIC daw::engine daw::model daw::juce)
daw_enable_warnings(daw_ui_desktop)
