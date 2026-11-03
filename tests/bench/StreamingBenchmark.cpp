// How many tracks play from disk, and how many record, at once.
//
// Build the release preset and run:  build/release/tests/daw_stream_bench
// Not part of CTest: timings depend on the machine, its disk and what else runs.
//
// Playback: N stereo tracks, each playing a 24-bit 48 kHz file from its own point,
// stream through a DiskStreamer (run between blocks, as its thread would) and are
// mixed, 20 seconds of audio as fast as possible. The real-time factor is audio time
// over the time taken; the read-ahead thread and the audio thread share it here.
// The files were just written, so they come from the operating system's cache: this
// measures the work of reading and converting, not the disk.
//
// Recording: N mono tracks of 24-bit 48 kHz go through the FIFOs and the writer
// thread to WAV files, 30 seconds each, as fast as the writer takes them.

#include "daw/engine/Mixer.h"
#include "daw/engine/io/DiskStreamer.h"
#include "daw/engine/io/RecordingWriter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{

using daw::engine::io::DiskStreamer;
using Clock = std::chrono::steady_clock;

constexpr double sampleRate = 48000.0;
constexpr int numFiles = 16;
constexpr int fileSeconds = 60;
constexpr int blockSize = 512;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::vector<juce::File> writeFiles(const juce::File& folder)
{
    std::vector<juce::File> files;
    juce::Random random(1);
    juce::AudioBuffer<float> audio(2, fileSeconds * static_cast<int>(sampleRate));

    for (int f = 0; f < numFiles; ++f)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                audio.setSample(channel, i, random.nextFloat() * 0.5f - 0.25f);

        const auto file = folder.getChildFile("stream" + juce::String(f) + ".wav");
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(sampleRate)
                                 .withNumChannels(2)
                                 .withBitsPerSample(24);
        auto writer = juce::WavAudioFormat().createWriterFor(stream, options);
        writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
        files.push_back(file);
    }

    return files;
}

struct PlaybackResult
{
    double realTimeFactor = 0.0;
    std::uint64_t dropouts = 0;
    std::size_t memory = 0;
};

PlaybackResult streamTracks(int numTracks, const std::vector<juce::File>& files)
{
    DiskStreamer::Settings settings;
    settings.sampleRate = sampleRate;
    settings.mode = DiskStreamer::Mode::manual;
    daw::engine::RenderClock clock;
    DiskStreamer streamer(settings, &clock);

    daw::engine::Arrangement arrangement;
    arrangement.sampleRate = sampleRate;
    for (int t = 0; t < numTracks; ++t)
    {
        juce::String error;
        auto source = streamer.open(files[static_cast<std::size_t>(t % numFiles)], error);
        const auto offset = static_cast<juce::int64>((t * 3.7) * sampleRate) % (30 * 48000);

        daw::engine::ArrangementTrack track;
        track.id = t + 1;
        track.leftGain = 0.5f;
        track.rightGain = 0.5f;
        track.segments.push_back({source.get(), 0, 25 * 48000, offset, 0, 0});
        arrangement.tracks.push_back(std::move(track));
        arrangement.sources.push_back(std::move(source));
    }
    arrangement.length = 25 * 48000;

    DiskStreamer::Playhead playhead{0, true};
    streamer.setPlan(arrangement);
    streamer.followPlayhead([&playhead] { return playhead; });

    daw::engine::Mixer mixer;
    mixer.prepare(sampleRate, blockSize);
    auto state = daw::engine::MixState::startingAt(arrangement);
    juce::AudioBuffer<float> output(2, blockSize);

    streamer.prefetch(0, 48000); // as the transport waits for the start
    const auto blocks = static_cast<int>(20 * sampleRate) / blockSize;
    const auto start = Clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        streamer.service();
        clock.begin();
        mixer.render(arrangement, state, playhead.position, output, 0, blockSize);
        clock.end();
        playhead.position += blockSize;
    }

    PlaybackResult result;
    result.realTimeFactor = 20.0 / secondsSince(start);
    result.dropouts = streamer.getDropouts();
    result.memory = streamer.getMemoryInUse();
    streamer.setPlan(daw::engine::Arrangement{});
    return result;
}

double recordTracks(int numTracks, const juce::File& folder)
{
    daw::engine::io::RecordingWriterSettings settings;
    settings.sampleRate = sampleRate;
    settings.bitsPerSample = 24;
    daw::engine::io::RecordingWriter writer(settings);

    std::vector<daw::engine::rt::AudioFifo*> fifos;
    for (int t = 0; t < numTracks; ++t)
    {
        const auto file = folder.getChildFile("record" + juce::String(t) + ".wav");
        fifos.push_back(&writer.addFile(file, 1));
    }
    if (writer.start().failed())
        return 0.0;

    std::vector<float> block(blockSize, 0.1f);
    const float* channels[] = {block.data()};
    const auto blocks = static_cast<int>(30 * sampleRate) / blockSize;

    const auto start = Clock::now();
    for (int b = 0; b < blocks; ++b)
        for (auto* fifo : fifos)
            while (!fifo->push(channels, blockSize))
                juce::Thread::yield();

    writer.finish();
    while (!writer.isFinished())
        juce::Thread::sleep(1);
    return 30.0 / secondsSince(start);
}

} // namespace

int main(int argc, char* argv[])
{
#if !defined(NDEBUG)
    std::printf("Warning: this is not an optimised build; use the release preset.\n\n");
#endif

    const auto folder =
        argc > 1 ? juce::File(juce::String(argv[1]))
                 : juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getNonexistentChildFile("daw-stream-bench", "");
    folder.createDirectory();

    std::printf("Writing %d stereo files of %d s...\n\n", numFiles, fileSeconds);
    const auto files = writeFiles(folder);

    std::printf("Playback from disk: stereo 24-bit 48 kHz tracks, blocks of %d\n\n", blockSize);
    std::printf("| Tracks | Real-time factor | Dropouts | Memory |\n");
    std::printf("|-------:|-----------------:|---------:|-------:|\n");
    for (const int tracks : {16, 64, 128, 256, 320, 384})
    {
        const auto result = streamTracks(tracks, files);
        std::printf("| %6d | %16.1f | %8llu | %4zu MB |\n", tracks, result.realTimeFactor,
                    static_cast<unsigned long long>(result.dropouts), result.memory >> 20);
        std::fflush(stdout);
    }

    std::printf("\nRecording: mono 24-bit 48 kHz tracks to WAV\n\n");
    std::printf("| Tracks | Real-time factor |\n");
    std::printf("|-------:|-----------------:|\n");
    for (const int tracks : {8, 32, 64, 128})
    {
        std::printf("| %6d | %16.1f |\n", tracks, recordTracks(tracks, folder));
        std::fflush(stdout);
    }

    folder.deleteRecursively();
    return 0;
}
