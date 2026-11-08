# Application entry point: creates the engine, the model and the user interface
# and connects them.

# Project files open from the Finder (macOS); the Windows installer and the Linux desktop
# file register them too (packaging/).
configure_file(resources/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.merge" @ONLY)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.merge" plist_to_merge)

# The icon is drawn by tools/make_app_icon.py; JUCE makes the .icns and the .ico from it.
juce_add_gui_app(daw_app
    PRODUCT_NAME "${DAW_PRODUCT_NAME}"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${DAW_COMPANY_NAME}"
    COMPANY_COPYRIGHT "Copyright (c) 2026 ${DAW_COMPANY_NAME}"
    BUNDLE_ID "${DAW_BUNDLE_ID}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png"
    PLIST_TO_MERGE "${plist_to_merge}"
    # macOS and iOS ask the user before an app records; this is the reason shown.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "${DAW_PRODUCT_NAME} records from your microphone and audio interface inputs.")

target_sources(daw_app PRIVATE
    src/AppController.cpp
    src/CrashMarker.cpp
    src/Main.cpp
    src/SmokeTest.cpp)

# The version (with a release candidate's suffix) comes from services::buildInfo().
target_compile_definitions(daw_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:daw_app,JUCE_PRODUCT_NAME>")

daw_use_juce_settings(daw_app)

target_link_libraries(daw_app PRIVATE daw::ui_desktop daw::appearance daw::services
    daw::controller daw::engine daw::model daw::juce)
daw_enable_warnings(daw_app)

# The plugin scanner, a program of its own next to the app's executable (inside the
# bundle on macOS), which the app starts for every plugin file it scans.
# WIN32: a Windows GUI program (JUCE gives it WinMain), so no console window opens.
add_executable(daw_plugin_scanner WIN32 scanner/ScannerMain.cpp)
set_target_properties(daw_plugin_scanner PROPERTIES OUTPUT_NAME "${DAW_PRODUCT_NAME} Plugin Scanner")
target_link_libraries(daw_plugin_scanner PRIVATE daw::controller daw::engine daw::juce)
daw_enable_warnings(daw_plugin_scanner)
add_dependencies(daw_app daw_plugin_scanner)
target_compile_definitions(daw_app PRIVATE
    "DAW_PLUGIN_SCANNER_NAME=\"$<TARGET_FILE_NAME:daw_plugin_scanner>\"")
add_custom_command(TARGET daw_app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:daw_plugin_scanner>"
        "$<TARGET_FILE_DIR:daw_app>/$<TARGET_FILE_NAME:daw_plugin_scanner>"
    VERBATIM)

# The licences of what the app contains travel with it: the app's own (LICENSE.txt), the
# fonts' (the SIL Open Font License asks for it), those of the plugin SDKs JUCE compiles
# in, of Monocypher and of sentry-native with its vendored parts (MIT, ISC and BSD ask for
# their notices), in the bundle's Resources on macOS, in a Licences folder next to the
# executable elsewhere. Each entry: the folder under Licences, then the file.
set(format_types "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types")
set(licences "")
foreach(family IN ITEMS ChakraPetch IBMPlexMono JetBrainsMono Michroma Rajdhani SpaceGrotesk)
    list(APPEND licences "${family}|${PROJECT_SOURCE_DIR}/appearance/fonts/${family}/OFL.txt")
endforeach()
list(APPEND licences "VST3_SDK|${format_types}/VST3_SDK/LICENSE.txt")
list(APPEND licences "Monocypher|${monocypher_SOURCE_DIR}/LICENCE.md")
list(APPEND licences "sentry-native|${sentry_SOURCE_DIR}/LICENSE")
# sentry-native's vendored mpack and jsmn carry their licence in their header's leading
# comment: it is written out as a file.
foreach(vendored IN ITEMS mpack jsmn)
    file(READ "${sentry_SOURCE_DIR}/vendor/${vendored}.h" header)
    string(FIND "${header}" "*/" comment_end)
    string(SUBSTRING "${header}" 0 ${comment_end} notice)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/licences/${vendored}.txt" "${notice}*/\n")
    list(APPEND licences "${vendored}|${CMAKE_CURRENT_BINARY_DIR}/licences/${vendored}.txt")
endforeach()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(library IN ITEMS lilv lv2 serd sord sratom)
        list(APPEND licences "${library}|${format_types}/LV2_SDK/${library}/COPYING")
    endforeach()
    list(APPEND licences "libunwind|${sentry_SOURCE_DIR}/vendor/libunwind/COPYING")
endif()

if(APPLE)
    target_sources(daw_app PRIVATE "${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md" "${PROJECT_SOURCE_DIR}/LICENSE.txt")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md" "${PROJECT_SOURCE_DIR}/LICENSE.txt"
        PROPERTIES
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
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/LICENSE.txt"
            "$<TARGET_FILE_DIR:daw_app>/Licences/LICENSE.txt"
        ${licence_commands}
        VERBATIM)
endif()
