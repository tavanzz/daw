# Audio engine: processing graph, transport, mixer, DSP and audio I/O.
# Never depends on any UI module (see CLAUDE.md, Architecture).

add_library(daw_engine STATIC
    src/Engine.cpp
    src/Transport.cpp
    src/dsp/Resampler.cpp
    src/io/AudioFileLoader.cpp)
add_library(daw::engine ALIAS daw_engine)

target_include_directories(daw_engine PUBLIC include PRIVATE src)
target_compile_features(daw_engine PUBLIC cxx_std_20)
target_link_libraries(daw_engine PUBLIC daw::juce)
daw_enable_warnings(daw_engine)
