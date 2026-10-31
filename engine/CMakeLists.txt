# Audio engine: processing graph, transport, mixer, DSP and audio I/O.
# Never depends on any UI module (see CLAUDE.md, Architecture).

add_library(daw_engine STATIC
    src/Arrangement.cpp
    src/AudioSource.cpp
    src/BeatGrid.cpp
    src/Engine.cpp
    src/Mixer.cpp
    src/OfflineRenderer.cpp
    src/Recorder.cpp
    src/Transport.cpp
    src/dsp/GainRamp.cpp
    src/dsp/InputMeters.cpp
    src/dsp/MeterBallistics.cpp
    src/dsp/Metronome.cpp
    src/dsp/PanLaw.cpp
    src/dsp/PeakLevelMeter.cpp
    src/dsp/Resampler.cpp
    src/dsp/WaveformPeaks.cpp
    src/io/AudioDeviceHost.cpp
    src/io/AudioFileLoader.cpp
    src/io/Bounce.cpp
    src/io/RecordingWriter.cpp
    src/io/SourcePool.cpp
    src/io/WavRepair.cpp
    src/rt/AudioFifo.cpp)
add_library(daw::engine ALIAS daw_engine)

target_include_directories(daw_engine PUBLIC include PRIVATE src)
target_compile_features(daw_engine PUBLIC cxx_std_20)
target_link_libraries(daw_engine PUBLIC daw::juce)
daw_enable_warnings(daw_engine)
