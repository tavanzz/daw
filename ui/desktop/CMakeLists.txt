# Desktop user interface (mouse and keyboard).

add_library(daw_ui_desktop STATIC
    src/Alerts.cpp
    src/AnimationClock.cpp
    src/AnimationLink.cpp
    src/AnimationsPanel.cpp
    src/ArrangeView.cpp
    src/AudioSettingsDialog.cpp
    src/AutomationLanes.cpp
    src/CompressorView.cpp
    src/EditorWindow.cpp
    src/EffectEditorWindow.cpp
    src/EffectSlots.cpp
    src/EqualiserView.cpp
    src/ExportDialog.cpp
    src/GenericEffectEditor.cpp
    src/HazardBand.cpp
    src/InstrumentViews.cpp
    src/LevelMeter.cpp
    src/Logo.cpp
    src/MainComponent.cpp
    src/MainWindow.cpp
    src/MixerView.cpp
    src/PanelWindow.cpp
    src/PianoRoll.cpp
    src/PluginBrowserPanel.cpp
    src/PluginEditorWindow.cpp
    src/PluginManagerPanel.cpp
    src/PrerenderedImages.cpp
    src/SendSlots.cpp
    src/StatusBar.cpp
    src/SystemMotion.cpp
    src/ThemeFonts.cpp
    src/ThemeLookAndFeels.cpp
    src/ThemeManager.cpp
    src/ThemePreviewsPanel.cpp
    src/ThemedLookAndFeel.cpp
    src/TimelineRuler.cpp
    src/TrackHeaderList.cpp
    src/TrackInputControls.cpp
    src/TrackLanes.cpp
    src/TransportBar.cpp)
add_library(daw::ui_desktop ALIAS daw_ui_desktop)

# What only the operating system knows: reduce motion, whether a window is covered.
if(APPLE)
    target_sources(daw_ui_desktop PRIVATE src/SystemQueries_apple.mm)
else()
    target_sources(daw_ui_desktop PRIVATE src/SystemQueries.cpp)
endif()

target_include_directories(daw_ui_desktop PUBLIC include PRIVATE src)
target_compile_features(daw_ui_desktop PUBLIC cxx_std_20)
target_link_libraries(daw_ui_desktop
    PUBLIC daw::appearance daw::controller daw::engine daw::model daw::juce)
daw_enable_warnings(daw_ui_desktop)
