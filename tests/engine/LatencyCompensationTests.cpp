#include "daw/engine/Engine.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/OfflineRenderer.h"
#include "daw/engine/TimelineHistory.h"

#include "support/RealtimeCheck.h"
#include "support/SimulatedDevice.h"
#include "support/TestArrangements.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using daw::engine::Arrangement;
using daw::engine::ArrangementBus;
using daw::engine::ArrangementSend;
using daw::engine::ArrangementTrack;
using daw::engine::ChainSlot;
using daw::engine::Engine;
using daw::engine::Mixer;
using daw::engine::MixState;
using daw::engine::OfflineRenderer;
using daw::engine::TimelineHistory;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::ProcessSetup;
using daw::test::GainEffect;
using daw::test::LatencyEffect;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 240; // 5 ms

ChainSlot effect(std::unique_ptr<daw::engine::fx::Effect> effectToHost)
{
    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = 512;
    ChainSlot slot;
    slot.instance = std::make_shared<EffectInstance>(std::move(effectToHost), setup);
    return slot;
}

ChainSlot delayBy(int samples)
{
    return effect(std::make_unique<LatencyEffect>(samples));
}

// One sample of value at position, silence elsewhere, in a 4000-sample stereo file.
std::shared_ptr<const daw::engine::MemoryAudioSource> impulse(float value, int position = 1000)
{
    juce::AudioBuffer<float> samples(2, 4000);
    samples.clear();
    samples.setSample(0, position, value);
    samples.setSample(1, position, value);
    return daw::test::makeSource(std::move(samples), sampleRate);
}

void addTrack(Arrangement& arrangement, juce::int64 id,
              const std::shared_ptr<const daw::engine::MemoryAudioSource>& source)
{
    ArrangementTrack track;
    track.id = id;
    track.segments.push_back({source.get(), 0, source->getLength(), 0, 0, 0});
    arrangement.tracks.push_back(std::move(track));
    arrangement.sources.push_back(source);
    arrangement.length = std::max(arrangement.length, source->getLength());
}

void addSend(ArrangementTrack& track, juce::int64 id, juce::int64 bus, int busIndex, bool preFader)
{
    ArrangementSend send;
    send.id = id;
    send.bus = bus;
    send.busIndex = busIndex;
    send.preFader = preFader;
    track.sends.push_back(send);
}

// Four tracks, each an impulse of its own size at 1000, through paths of different
// latency:
//   track 1: no effect; post-fader send to bus 21 (latency 10)
//   track 2: an effect of latency 100
//   track 3: an effect of latency 30; pre-fader send to bus 20 (latency 50)
//   track 4: no effect; post-fader send to bus 20
//   master: an effect of latency 7
// Slowest path: track 2 (100), so every path meets at 100 and the output lags 107.
std::unique_ptr<Arrangement> makeRouting()
{
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    addTrack(*arrangement, 1, impulse(1.0f / 16.0f));
    addTrack(*arrangement, 2, impulse(1.0f / 8.0f));
    addTrack(*arrangement, 3, impulse(1.0f / 4.0f));
    addTrack(*arrangement, 4, impulse(1.0f / 2.0f));

    ArrangementBus slow;
    slow.id = 20;
    slow.inserts.slots.push_back(delayBy(50));
    ArrangementBus fast;
    fast.id = 21;
    fast.inserts.slots.push_back(delayBy(10));
    arrangement->buses.push_back(std::move(slow));
    arrangement->buses.push_back(std::move(fast));

    addSend(arrangement->tracks[0], 30, 21, 1, false);
    arrangement->tracks[1].inserts.slots.push_back(delayBy(100));
    arrangement->tracks[2].inserts.slots.push_back(delayBy(30));
    addSend(arrangement->tracks[2], 31, 20, 0, true);
    addSend(arrangement->tracks[3], 32, 20, 0, false);
    arrangement->masterInserts.slots.push_back(delayBy(7));
    return arrangement;
}

// Everything that reaches the output: the four tracks plus what the buses get.
constexpr float routedSum = 1.0f / 16 + 1.0f / 8 + 1.0f / 4 + 1.0f / 2 // tracks
                            + 1.0f / 4 + 1.0f / 2                       // bus 20
                            + 1.0f / 16;                                // bus 21

bool onlyAt(const std::vector<float>& samples, std::size_t index, float value)
{
    for (std::size_t i = 0; i < samples.size(); ++i)
        if (samples[i] != (i == index ? value : 0.0f))
            return false;
    return true;
}

std::vector<float> renderLeft(Mixer& mixer, const Arrangement& arrangement, MixState& state,
                              int length, const std::vector<int>& pattern,
                              std::size_t& heapOperations)
{
    std::vector<float> left;
    juce::AudioBuffer<float> block(2, 4096);
    std::size_t index = 0;
    for (int done = 0; done < length;)
    {
        const int piece = std::min(pattern[index++ % pattern.size()], length - done);
        heapOperations += daw::test::heapOperationsDuring(
            [&] { mixer.render(arrangement, state, done, block, 0, piece); });
        left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + piece);
        done += piece;
    }
    return left;
}

struct Rendered
{
    std::vector<float> samples;
    std::size_t heapOperations = 0;
};

Rendered process(Engine& engine, int numBlocks, int blockSize = 256)
{
    Rendered rendered;
    juce::AudioBuffer<float> block(2, blockSize);
    for (int i = 0; i < numBlocks; ++i)
    {
        rendered.heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        rendered.samples.insert(rendered.samples.end(), block.getReadPointer(0),
                                block.getReadPointer(0) + blockSize);
    }
    return rendered;
}

float largestStep(const std::vector<float>& samples)
{
    float largest = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

// Beat b at 2000 b, four to a bar.
class EvenGrid final : public daw::engine::BeatGrid
{
public:
    juce::int64 beatPosition(juce::int64 beat) const noexcept override { return beat * 2000; }
    int beatsPerBar() const noexcept override { return 4; }
    double approximateBeatLength() const noexcept override { return 2000.0; }
};

} // namespace

TEST_CASE("Every path is delayed to meet the slowest one, sends and buses too",
          "[engine][latency]")
{
    const auto arrangement = makeRouting();
    const auto state = MixState::startingAt(*arrangement);

    CHECK(state.mixLatency == 100);
    CHECK(state.latency == 107);
    CHECK(state.tracks[0].outputDelay == 100);
    CHECK(state.tracks[1].outputDelay == 0);
    CHECK(state.tracks[2].outputDelay == 70);
    CHECK(state.tracks[3].outputDelay == 100);
    CHECK(state.buses[0].inputLatency == 30);
    CHECK(state.buses[1].inputLatency == 0);
    CHECK(state.tracks[2].sends[0].delay == 0);  // track 3 is bus 20's slowest
    CHECK(state.tracks[3].sends[0].delay == 30); // track 4 waits for it
    CHECK(state.tracks[0].sends[0].delay == 0);
    CHECK(state.buses[0].outputDelay == 20);  // 100 - 30 - 50
    CHECK(state.buses[1].outputDelay == 90);  // 100 - 0 - 10
}

TEST_CASE("With effects of known latency on tracks, buses and sends, all stays aligned to the sample",
          "[engine][latency]")
{
    const auto pattern = GENERATE(std::vector<int>{64}, std::vector<int>{1, 7, 100, 333, 4096});
    const auto arrangement = makeRouting();

    Mixer mixer;
    mixer.prepare(sampleRate, 4096);
    auto state = MixState::startingAt(*arrangement);
    std::size_t heapOperations = 0;
    const auto left = renderLeft(mixer, *arrangement, state, 3000, pattern, heapOperations);

    // One impulse, the sum of every path, 107 samples after where they all are.
    CAPTURE(pattern);
    CHECK(onlyAt(left, 1107, routedSum));
    CHECK(heapOperations == 0);

    // The export makes up for the latency: the impulse is where it is on the timeline.
    const auto again = makeRouting();
    OfflineRenderer renderer(*again);
    juce::AudioBuffer<float> exported(2, 3000);
    renderer.render(exported);
    CHECK(renderer.getLatency() == 107);
    const std::vector<float> exportedLeft(exported.getReadPointer(0),
                                          exported.getReadPointer(0) + 3000);
    CHECK(onlyAt(exportedLeft, 1000, routedSum));
}

TEST_CASE("The playhead and the metronome follow what is heard", "[engine][latency]")
{
    const auto clicksOnly = [](int latency) {
        auto arrangement = std::make_unique<Arrangement>();
        arrangement->sampleRate = sampleRate;
        arrangement->length = 100000;
        arrangement->beats = std::make_shared<EvenGrid>();
        arrangement->metronomeEnabled = true;
        arrangement->metronomeGain = 1.0f;
        if (latency > 0)
            arrangement->masterInserts.slots.push_back(delayBy(latency));
        return arrangement;
    };

    Engine plain;
    plain.prepare(sampleRate, 256);
    plain.setArrangement(clicksOnly(0));
    plain.play();
    Engine delayed;
    delayed.prepare(sampleRate, 256);
    delayed.setArrangement(clicksOnly(107));
    delayed.play();

    // Nothing of the first block is heard before sample 107.
    const auto first = process(delayed, 1);
    CHECK(delayed.getLatency() == 107);
    CHECK(delayed.getPlayheadPosition() == 256 - 107);

    const auto rest = process(delayed, 39);
    const auto reference = process(plain, 40);
    std::vector<float> samples = first.samples;
    samples.insert(samples.end(), rest.samples.begin(), rest.samples.end());

    // The same clicks, 107 samples later.
    CHECK(std::all_of(samples.begin(), samples.begin() + 107, [](float s) { return s == 0.0f; }));
    bool shifted = true;
    for (std::size_t i = 107; i < samples.size(); ++i)
        shifted = shifted && samples[i] == reference.samples[i - 107];
    CHECK(shifted);
    CHECK(*std::max_element(samples.begin(), samples.end()) > 0.1f);
    CHECK(delayed.getPlayheadPosition() == 40 * 256 - 107);
    CHECK(rest.heapOperations == 0);
}

TEST_CASE("The timeline history knows what was read when, through loops and stops",
          "[engine][latency]")
{
    TimelineHistory history;
    history.reset(0, 500);
    history.record({100, 500, TimelineHistory::Kind::playing, true});
    history.record({300, 700, TimelineHistory::Kind::playing, true}); // goes straight on
    history.record({400, 200, TimelineHistory::Kind::playing, true}); // a loop back to 200
    history.record({450, 250, TimelineHistory::Kind::stopped, false});

    CHECK(history.at(50).position == 500);
    CHECK(history.at(50).kind == TimelineHistory::Kind::stopped);
    CHECK(history.at(100).position == 500);
    CHECK(history.at(399).position == 799);
    CHECK(history.at(400).position == 200);
    CHECK(history.at(449).position == 249);
    CHECK(history.at(1000).position == 250);
    CHECK(history.at(1000).kind == TimelineHistory::Kind::stopped);

    std::vector<std::pair<int, juce::int64>> pieces;
    history.forEach(350, 200, [&](int offset, int length, const TimelineHistory::Stretch& stretch) {
        pieces.emplace_back(length, stretch.position);
        CHECK(offset >= 0);
    });
    CHECK(pieces == std::vector<std::pair<int, juce::int64>>{{50, 750}, {50, 200}, {100, 250}});
}

TEST_CASE("The export equals real-time playback with latency, sample for sample",
          "[engine][latency][bounce]")
{
    const int blockSize = GENERATE(64, 441);
    const auto makeSong = [] {
        auto arrangement = makeRouting();
        // Every sample different from the start, through the same routing.
        juce::AudioBuffer<float> noise(2, 20000);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 20000; ++i)
                noise.setSample(channel, i, i < 1000 ? 0.0f : 0.25f * daw::test::testInputSample(channel, i));
        const auto source = daw::test::makeSource(std::move(noise), sampleRate);
        for (auto& track : arrangement->tracks)
            track.segments = {{source.get(), 0, 20000, 0, 0, 0}};
        arrangement->sources.push_back(source);
        arrangement->tracks[0].inserts.slots.push_back(effect(std::make_unique<GainEffect>(0.5f)));
        arrangement->length = 20000;
        return arrangement;
    };

    const auto song = makeSong();
    OfflineRenderer renderer(*song);
    juce::AudioBuffer<float> exported(2, 20000);
    renderer.render(exported);

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(makeSong());
    engine.play();
    const auto played = process(engine, (20000 + 107) / blockSize + 2, blockSize);

    // Sample k of the export is heard 107 samples after playback started; the music
    // starts at 1000, after the transport's fade-in.
    bool identical = true;
    for (int k = 0; k < 20000; ++k)
        identical = identical && std::bit_cast<std::uint32_t>(played.samples[static_cast<std::size_t>(k + 107)]) ==
                                     std::bit_cast<std::uint32_t>(exported.getSample(0, k));
    CHECK(identical);
    CHECK(exported.getMagnitude(0, 0, 20000) > 0.1f);
    CHECK(played.heapOperations == 0);
}

TEST_CASE("A change of latency while playing dips the output instead of jumping",
          "[engine][latency]")
{
    const auto source = daw::test::makeConstantSource(2, 480000, 0.5f, sampleRate);
    Engine engine;
    engine.prepare(sampleRate, 256);
    engine.setArrangement(daw::test::makeWholeFileArrangement(source));
    engine.play();
    (void) process(engine, 4);

    // An effect without latency: no dip (it fades in by itself).
    auto gain = daw::test::makeWholeFileArrangement(source);
    gain->tracks[0].inserts.slots.push_back(effect(std::make_unique<GainEffect>(1.0f)));
    engine.setArrangement(std::move(gain));
    const auto noDip = process(engine, 4);
    CHECK(std::all_of(noDip.samples.begin(), noDip.samples.end(), [](float s) { return s == 0.5f; }));

    // 1000 samples of latency on the master: fade out, 1000 samples of silence while
    // the delay fills, fade in.
    auto delayed = daw::test::makeWholeFileArrangement(source);
    delayed->tracks[0].inserts.slots.push_back(effect(std::make_unique<GainEffect>(1.0f)));
    delayed->masterInserts.slots.push_back(delayBy(1000));
    engine.setArrangement(std::move(delayed));
    const auto dip = process(engine, 12);

    CHECK(dip.heapOperations == 0);
    CHECK(dip.samples[fadeLength - 1] == 0.0f);
    CHECK(std::all_of(dip.samples.begin() + fadeLength, dip.samples.begin() + 256 + 1000,
                      [](float s) { return s == 0.0f; }));
    CHECK(dip.samples.back() == 0.5f);
    CHECK(largestStep(dip.samples) <= 0.5f / fadeLength * 1.001f);
    CHECK(engine.getLatency() == 1000);
}

TEST_CASE("Recordings line up with what is heard when effects add latency", "[engine][latency]")
{
    // As "Recordings line up to the sample with what was played", with the played
    // track going through an effect of latency 333: what is heard is 333 later, and
    // the recording follows it.
    const auto ramp = [] {
        juce::AudioBuffer<float> samples(1, 400000);
        for (int i = 0; i < samples.getNumSamples(); ++i)
            samples.setSample(0, i, static_cast<float>(i + 1) / 1048576.0f);
        return daw::test::makeSource(std::move(samples), sampleRate);
    }();
    auto arrangement = daw::test::makeWholeFileArrangement(ramp, 1);
    arrangement->tracks[0].inserts.slots.push_back(delayBy(333));
    ArrangementTrack recording;
    recording.id = 2;
    recording.inputChannel = 0;
    recording.numInputChannels = 1;
    recording.armed = true;
    arrangement->tracks.push_back(recording);

    const int deviceLatency = 512 + 37;
    Engine engine;
    daw::test::SimulatedDevice device(engine, sampleRate, 2, {64, 512, 7});
    device.connectLoopback(0, 0, deviceLatency);
    device.start();
    engine.setArrangement(std::move(arrangement));

    daw::engine::rt::AudioFifo fifo(1, 200000);
    daw::engine::RecordingSetup setup;
    setup.targets.push_back({2, 0, 1, &fifo});
    setup.startPosition = 20000;
    setup.latency = deviceLatency;
    engine.startRecording(setup);

    std::size_t heapOperations = device.run(60000);
    engine.stop();
    heapOperations += device.run(deviceLatency + 2000);
    CHECK(heapOperations == 0);

    const auto status = engine.getRecordingStatus();
    REQUIRE(status.state == daw::engine::RecordingState::complete);
    CHECK(status.firstPosition == 20000);

    std::vector<float> recorded(static_cast<std::size_t>(fifo.getNumReady()));
    float* channels[] = {recorded.data()};
    fifo.pop(channels, fifo.getNumReady());
    REQUIRE(recorded.size() > 30000);

    bool aligned = true;
    for (std::size_t k = 0; k < recorded.size(); ++k)
    {
        const float sample = static_cast<float>(20000 + k + 1) / 1048576.0f;
        const float played = k < static_cast<std::size_t>(fadeLength)
                                 ? sample * (static_cast<float>(k + 1) / static_cast<float>(fadeLength))
                                 : sample;
        aligned = aligned && std::bit_cast<std::uint32_t>(recorded[k]) ==
                                 std::bit_cast<std::uint32_t>(played);
    }
    CHECK(aligned);
}
