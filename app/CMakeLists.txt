# Application entry point: creates the engine, the model and the user interface
# and connects them.

juce_add_gui_app(daw_app
    PRODUCT_NAME "${DAW_PRODUCT_NAME}"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${DAW_COMPANY_NAME}"
    BUNDLE_ID "${DAW_BUNDLE_ID}")

target_sources(daw_app PRIVATE
    src/AppController.cpp
    src/Main.cpp)

target_compile_definitions(daw_app PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:daw_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:daw_app,JUCE_VERSION>")

target_link_libraries(daw_app PRIVATE daw::ui_desktop daw::controller daw::engine daw::model daw::juce)
daw_enable_warnings(daw_app)
