# Application entry point: creates the engine, the model and the user interface
# and connects them.

juce_add_gui_app(daw_app
    PRODUCT_NAME "${DAW_PRODUCT_NAME}"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${DAW_COMPANY_NAME}"
    BUNDLE_ID "${DAW_BUNDLE_ID}"
    # macOS and iOS ask the user before an app records; this is the reason shown.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "${DAW_PRODUCT_NAME} records from your microphone and audio interface inputs.")

target_sources(daw_app PRIVATE
    src/AppController.cpp
    src/Main.cpp)

target_compile_definitions(daw_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:daw_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:daw_app,JUCE_VERSION>")

target_link_libraries(daw_app PRIVATE daw::ui_desktop daw::appearance daw::controller daw::engine
    daw::model daw::juce)
daw_enable_warnings(daw_app)

# The plugin scanner, a program of its own next to the app's executable (inside the
# bundle on macOS), which the app starts for every plugin file it scans.
# WIN32: a Windows GUI program (JUCE gives it WinMain), so no console window opens.
add_executable(daw_plugin_scanner WIN32 scanner/ScannerMain.cpp)
set_target_properties(daw_plugin_scanner PROPERTIES OUTPUT_NAME "${DAW_PRODUCT_NAME} Plugin Scanner")
target_link_libraries(daw_plugin_scanner PRIVATE daw::controller daw::engine daw::juce)
daw_enable_warnings(daw_plugin_scanner)
add_dependencies(daw_app daw_plugin_scanner)
add_custom_command(TARGET daw_app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:daw_plugin_scanner>"
        "$<TARGET_FILE_DIR:daw_app>/$<TARGET_FILE_NAME:daw_plugin_scanner>"
    VERBATIM)

# The licences of what the app contains travel with it: the fonts' (the SIL Open Font
# License asks for it) and those of the plugin SDKs JUCE compiles in (MIT and ISC ask
# for their notices), in the bundle's Resources on macOS, in a Licences folder next to
# the executable elsewhere. Each entry: the folder under Licences, then the file.
set(format_types "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types")
set(licences "")
foreach(family IN ITEMS ChakraPetch IBMPlexMono JetBrainsMono Michroma Rajdhani SpaceGrotesk)
    list(APPEND licences "${family}|${PROJECT_SOURCE_DIR}/appearance/fonts/${family}/OFL.txt")
endforeach()
list(APPEND licences "VST3_SDK|${format_types}/VST3_SDK/LICENSE.txt")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(library IN ITEMS lilv lv2 serd sord sratom)
        list(APPEND licences "${library}|${format_types}/LV2_SDK/${library}/COPYING")
    endforeach()
endif()

if(APPLE)
    target_sources(daw_app PRIVATE "${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md" PROPERTIES
        HEADER_FILE_ONLY TRUE
        MACOSX_PACKAGE_LOCATION "Resources/Licences")

    foreach(entry IN LISTS licences)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 folder)
        list(GET parts 1 licence)
        target_sources(daw_app PRIVATE "${licence}")
        set_source_files_properties("${licence}" PROPERTIES
            HEADER_FILE_ONLY TRUE
            MACOSX_PACKAGE_LOCATION "Resources/Licences/${folder}")
    endforeach()
else()
    set(licence_commands "")
    foreach(entry IN LISTS licences)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 folder)
        list(GET parts 1 licence)
        get_filename_component(name "${licence}" NAME)
        list(APPEND licence_commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:daw_app>/Licences/${folder}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${licence}" "$<TARGET_FILE_DIR:daw_app>/Licences/${folder}/${name}")
    endforeach()

    add_custom_command(TARGET daw_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:daw_app>/Licences"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md"
            "$<TARGET_FILE_DIR:daw_app>/Licences/THIRD_PARTY.md"
        ${licence_commands}
        VERBATIM)
endif()
