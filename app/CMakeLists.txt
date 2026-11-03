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

# The fonts' licences travel with the app (the SIL Open Font License asks for it):
# in the bundle's Resources on macOS, in a Licences folder next to the executable
# elsewhere.
set(font_licences
    ChakraPetch IBMPlexMono JetBrainsMono Michroma Rajdhani SpaceGrotesk)

if(APPLE)
    target_sources(daw_app PRIVATE "${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md" PROPERTIES
        HEADER_FILE_ONLY TRUE
        MACOSX_PACKAGE_LOCATION "Resources/Licences")

    foreach(family IN LISTS font_licences)
        set(licence "${PROJECT_SOURCE_DIR}/appearance/fonts/${family}/OFL.txt")
        target_sources(daw_app PRIVATE "${licence}")
        set_source_files_properties("${licence}" PROPERTIES
            HEADER_FILE_ONLY TRUE
            MACOSX_PACKAGE_LOCATION "Resources/Licences/${family}")
    endforeach()
else()
    set(licence_commands "")
    foreach(family IN LISTS font_licences)
        list(APPEND licence_commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:daw_app>/Licences/${family}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PROJECT_SOURCE_DIR}/appearance/fonts/${family}/OFL.txt"
                "$<TARGET_FILE_DIR:daw_app>/Licences/${family}/OFL.txt")
    endforeach()

    add_custom_command(TARGET daw_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:daw_app>/Licences"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/docs/THIRD_PARTY.md"
            "$<TARGET_FILE_DIR:daw_app>/Licences/THIRD_PARTY.md"
        ${licence_commands}
        VERBATIM)
endif()
