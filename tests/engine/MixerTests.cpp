#include "daw/engine/Arrangement.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/dsp/GainRamp.h"
#include "daw/engine/dsp/PanLaw.h"

#include "support/RealtimeCheck.h"
#include "support/SignalAnalysis.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using daw::engine::Arrangement;
using daw::engine::ArrangementTrack;
using daw::engine::AudioData;
using daw::engine::ClipPlacement;
using daw::engine::ClipSegment;
using daw::engine::MemoryAudioSource;
using daw::engine::Mixer;
using daw::engine::MixState;
using daw::engine::dsp::GainRamp;
using daw::engine::dsp::panGains;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int maximumBlock = 4096;

std::shared_ptr<const MemoryAudioSource> makeSource(const juce::AudioBuffer<float>& samples)
{
    AudioData data;
    data.sampleRate = sampleRate;
    data.samples = samples;
    return std::make_shared<const MemoryAudioSource>(std::move(data));
}

std::shared_ptr<const MemoryAudioSource> makeConstant(int channels, int length, float value)
{
    juce::AudioBuffer<float> samples(channels, length);
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), value, length);
    return makeSource(samples);
}

// Left channel n = n + 1, right channel = -(n + 1): every sample says where it came from.
std::shared_ptr<const MemoryAudioSource> makeRamp(int length)
{
    juce::AudioBuffer<float> samples(2, length);
    for (int n = 0; n < length; ++n)
    {
        samples.setSample(0, n, static_cast<float>(n + 1));
        samples.setSample(1, n, -static_cast<float>(n + 1));
    }
    return makeSource(samples);
}

std::shared_ptr<const MemoryAudioSource> makeNoise(int channels, int length, int seed)
{
    juce::Random random(seed);
    juce::AudioBuffer<float> samples(channels, length);
    for (int channel = 0; channel < channels; ++channel)
        for (int n = 0; n < length; ++n)
            samples.setSample(channel, n, random.nextFloat() * 2.0f - 1.0f);
    return makeSource(samples);
}

// A track that plays one whole source from position start.
ArrangementTrack trackWith(juce::int64 id, const std::shared_ptr<const MemoryAudioSource>& source,
                           juce::int64 start, float left = 1.0f, float right = 1.0f)
{
    ArrangementTrack track;
    track.id = id;
    track.leftGain = left;
    track.rightGain = right;
    track.segments.push_back({source.get(), start, start + source->getLength(), 0, 0, 0});
    return track;
}

Arrangement arrangementOf(std::vector<ArrangementTrack> tracks,
                          std::vector<std::shared_ptr<const MemoryAudioSource>> sources)
{
    Arrangement arrangement;
    arrangement.sampleRate = sampleRate;
    arrangement.tracks = std::move(tracks);
    for (auto& source : sources)
        arrangement.sources.push_back(std::move(source));
    arrangement.updateLayoutHash();
    return arrangement;
}

// Renders [start, start + length) in blocks of the given sizes (repeated), returning
// the output and the heap operations made while rendering.
struct Render
{
    juce::AudioBuffer<float> output;
    std::size_t heapOperations = 0;
};

Render render(Mixer& mixer, const Arrangement& arrangement, MixState& state, juce::int64 start,
              int length, const std::vector<int>& blockSizes)
{
    Render result;
    result.output.setSize(2, length);
    juce::AudioBuffer<float> block(2, maximumBlock);

    int done = 0;
    std::size_t next = 0;
    while (done < length)
    {
        const int size = std::min(blockSizes[next++ % blockSizes.size()], length - done);
        result.heapOperations += daw::test::heapOperationsDuring(
            [&] { mixer.render(arrangement, state, start + done, block, 0, size); });
        for (int channel = 0; channel < 2; ++channel)
            result.output.copyFrom(channel, done, block, channel, 0, size);
        done += size;
    }

    return result;
}

Render renderFresh(const Arrangement& arrangement, juce::int64 start, int length,
                   const std::vector<int>& blockSizes)
{
    Mixer mixer;
    mixer.prepare(sampleRate, maximumBlock);
    auto state = MixState::startingAt(arrangement);
    return render(mixer, arrangement, state, start, length, blockSizes);
}

bool bitIdentical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        if (std::memcmp(a.getReadPointer(channel), b.getReadPointer(channel),
                        sizeof(float) * static_cast<std::size_t>(a.getNumSamples())) != 0)
            return false;

    return true;
}

} // namespace

TEST_CASE("The pan law: unity in the centre, +3 dB on one side at the extremes",
          "[engine][mixer][pan]")
{
    const auto centre = panGains(0.0);
    CHECK(centre.left == 1.0f);
    CHECK(centre.right == 1.0f);

    const auto left = panGains(-1.0);
    CHECK(left.left == static_cast<float>(std::numbers::sqrt2));
    CHECK(left.right == 0.0f);
    CHECK_THAT(daw::test::toDecibels(left.left), WithinAbs(3.0103, 0.0001));

    const auto right = panGains(1.0);
    CHECK(right.left == 0.0f);
    CHECK(right.right == static_cast<float>(std::numbers::sqrt2));

    // Constant power everywhere in between, and mirror images either side.
    for (double pan = -1.0; pan <= 1.0; pan += 0.125)
    {
        const auto gains = panGains(pan);
        CHECK_THAT(gains.left * gains.left + gains.right * gains.right, WithinAbs(2.0, 1e-6));
        const auto mirrored = panGains(-pan);
        CHECK(gains.left == mirrored.right);
    }

    // Halfway left: cos/sin of 22.5 degrees, times sqrt 2.
    const auto halfLeft = panGains(-0.5);
    const double eighthTurn = std::numbers::pi / 8.0;
    CHECK_THAT(halfLeft.left, WithinRel(std::numbers::sqrt2 * std::cos(eighthTurn), 1e-6));
    CHECK_THAT(halfLeft.right, WithinRel(std::numbers::sqrt2 * std::sin(eighthTurn), 1e-6));

    // Out of range values are clamped.
    CHECK(panGains(-7.0).left == left.left);
    CHECK(panGains(std::nan("")).left == 1.0f);
}

TEST_CASE("A gain ramp moves in a straight line and lands exactly on its target",
          "[engine][mixer][ramp]")
{
    GainRamp ramp;
    ramp.reset(1.0f);
    ramp.setTarget(0.0f, 4);

    std::vector<float> samples(6, 1.0f);
    float* channels[] = {samples.data()};
    ramp.apply(channels, 1, 6);
    CHECK(samples == std::vector<float>{0.75f, 0.5f, 0.25f, 0.0f, 0.0f, 0.0f});
    CHECK_FALSE(ramp.isRamping());

    // Retargeting mid-ramp starts from the current gain.
    ramp.reset(0.0f);
    ramp.setTarget(1.0f, 4);
    ramp.skip(2);
    CHECK(ramp.getCurrent() == 0.5f);
    ramp.setTarget(0.5f, 2);
    std::vector<float> more(3, 1.0f);
    float* moreChannels[] = {more.data()};
    ramp.apply(moreChannels, 1, 3);
    CHECK(more == std::vector<float>{0.5f, 0.5f, 0.5f});

    // Setting the same target again does not restart anything.
    ramp.setTarget(0.5f, 100);
    CHECK_FALSE(ramp.isRamping());
}

TEST_CASE("Tracks add up sample for sample", "[engine][mixer]")
{
    auto a = makeNoise(2, 3000, 1);
    auto b = makeNoise(1, 2000, 2);
    auto c = makeConstant(2, 1000, 0.25f);

    const auto arrangement =
        arrangementOf({trackWith(1, a, 0, 0.5f, 0.75f), trackWith(2, b, 500, 1.25f, 0.0f),
                       trackWith(3, c, 2900, 1.0f, 1.0f)},
                      {a, b, c});

    const auto rendered = renderFresh(arrangement, 0, 4000, {256});
    CHECK(rendered.heapOperations == 0);

    // The same sums, computed directly in the mixer's order (track 1, 2, 3).
    for (int n = 0; n < 4000; ++n)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const float gainA = channel == 0 ? 0.5f : 0.75f;
            const float gainB = channel == 0 ? 1.25f : 0.0f;
            float expected = 0.0f;
            expected += n < 3000 ? a->getSamples().getSample(channel, n) * gainA : 0.0f;
            expected += (n >= 500 && n < 2500) ? b->getSamples().getSample(0, n - 500) * gainB
                                               : 0.0f;
            expected += (n >= 2900 && n < 3900) ? 0.25f : 0.0f;
            REQUIRE(rendered.output.getSample(channel, n) == expected);
        }
    }
}

TEST_CASE("A neutral track plays its file bit for bit", "[engine][mixer]")
{
    auto source = makeNoise(2, 5000, 3);
    const auto arrangement = arrangementOf({trackWith(1, source, 0)}, {source});

    const auto rendered = renderFresh(arrangement, 0, 5000, {512});
    CHECK(bitIdentical(rendered.output, source->getSamples()));
}

TEST_CASE("Clips start and end on their exact sample, also in the middle of a block",
          "[engine][mixer]")
{
    const int blockSize = GENERATE(1, 7, 64, 256, 1000);
    auto one = makeConstant(1, 500, 1.0f);
    const auto arrangement = arrangementOf({trackWith(1, one, 1000)}, {one});

    const auto rendered = renderFresh(arrangement, 0, 2000, {blockSize});

    for (int channel = 0; channel < 2; ++channel)
        for (int n = 0; n < 2000; ++n)
            REQUIRE(rendered.output.getSample(channel, n) ==
                    (n >= 1000 && n < 1500 ? 1.0f : 0.0f));
}

TEST_CASE("A clip plays its file from its offset", "[engine][mixer]")
{
    auto ramp = makeRamp(1000);
    ArrangementTrack track;
    track.id = 1;
    track.segments.push_back({ramp.get(), 300, 400, 50, 0, 0});
    const auto arrangement = arrangementOf({track}, {ramp});

    const auto rendered = renderFresh(arrangement, 250, 200, {37});

    CHECK(rendered.output.getSample(0, 49) == 0.0f);
    CHECK(rendered.output.getSample(0, 50) == 51.0f); // timeline 300 = source sample 50
    CHECK(rendered.output.getSample(1, 50) == -51.0f);
    CHECK(rendered.output.getSample(0, 149) == 150.0f);
    CHECK(rendered.output.getSample(0, 150) == 0.0f);
}

TEST_CASE("The clip on top hides what it covers", "[engine][mixer][flatten]")
{
    auto below = makeRamp(10000);
    auto above = makeRamp(10000);
    const int fade = 4;

    SECTION("Covering the middle cuts the clip below in two, with fades at the cuts")
    {
        const std::vector<ClipPlacement> clips{{below, 0, 1000, 0}, {above, 400, 600, 0}};
        const auto segments = daw::engine::flattenClips(clips, fade);

        REQUIRE(segments.size() == 3);
        CHECK(segments[0] == ClipSegment{below.get(), 0, 400, 0, 0, fade});
        CHECK(segments[1] == ClipSegment{above.get(), 400, 600, 0, 0, fade});
        CHECK(segments[2] == ClipSegment{below.get(), 600, 1000, 600, fade, fade});
    }

    SECTION("The stacking order decides, not the position")
    {
        const std::vector<ClipPlacement> clips{{above, 400, 600, 0}, {below, 0, 1000, 0}};
        const auto segments = daw::engine::flattenClips(clips, fade);

        REQUIRE(segments.size() == 1);
        CHECK(segments[0] == ClipSegment{below.get(), 0, 1000, 0, 0, fade});
    }

    SECTION("A whole file has no fades; a trimmed one fades where it was cut")
    {
        auto file = makeRamp(800);
        const std::vector<ClipPlacement> whole{{file, 100, 900, 0}};
        CHECK(daw::engine::flattenClips(whole, fade)[0] ==
              ClipSegment{file.get(), 100, 900, 0, 0, 0});

        const std::vector<ClipPlacement> trimmed{{file, 100, 300, 10}};
        CHECK(daw::engine::flattenClips(trimmed, fade)[0] ==
              ClipSegment{file.get(), 100, 300, 10, fade, fade});

        // Fades never take more than the segment: half each when both ends are cut.
        const std::vector<ClipPlacement> tiny{{file, 0, 5, 10}};
        CHECK(daw::engine::flattenClips(tiny, fade)[0] ==
              ClipSegment{file.get(), 0, 5, 10, 2, 2});
    }

    SECTION("A clip whose file is missing still hides what is below it")
    {
        const std::vector<ClipPlacement> clips{{below, 0, 1000, 0}, {nullptr, 200, 300, 0}};
        const auto segments = daw::engine::flattenClips(clips, fade);

        REQUIRE(segments.size() == 2);
        CHECK(segments[0].end == 200);
        CHECK(segments[1].start == 300);
    }

    SECTION("Adjacent and nested clips")
    {
        const std::vector<ClipPlacement> clips{
            {below, 0, 100, 0}, {above, 100, 200, 100}, {below, 50, 150, 50}};
        const auto segments = daw::engine::flattenClips(clips, fade);

        REQUIRE(segments.size() == 3);
        CHECK(segments[0] == ClipSegment{below.get(), 0, 50, 0, 0, fade});
        CHECK(segments[1] == ClipSegment{below.get(), 50, 150, 50, fade, fade});
        CHECK(segments[2] == ClipSegment{above.get(), 150, 200, 150, fade, fade});
    }
}

TEST_CASE("Fades on cut edges are exact linear ramps", "[engine][mixer][flatten]")
{
    auto one = makeConstant(2, 1000, 1.0f);
    ArrangementTrack track;
    track.id = 1;
    track.segments.push_back({one.get(), 100, 200, 10, 4, 4});
    const auto arrangement = arrangementOf({track}, {one});

    const auto rendered = renderFresh(arrangement, 98, 105, {3});
    const auto at = [&rendered](int timeline) {
        return rendered.output.getSample(0, timeline - 98);
    };

    CHECK(at(99) == 0.0f);
    CHECK(at(100) == 0.25f);
    CHECK(at(101) == 0.5f);
    CHECK(at(102) == 0.75f);
    CHECK(at(103) == 1.0f);
    CHECK(at(195) == 1.0f);
    CHECK(at(196) == 1.0f);
    CHECK(at(197) == 0.75f);
    CHECK(at(198) == 0.5f);
    CHECK(at(199) == 0.25f);
    CHECK(at(200) == 0.0f);
}

TEST_CASE("Volume and pan changes ramp without steps", "[engine][mixer][ramp]")
{
    auto one = makeConstant(2, 48000, 1.0f);
    const auto before = arrangementOf({trackWith(7, one, 0, 1.0f, 1.0f)}, {one});
    const auto gains = panGains(-0.5);
    const auto after =
        arrangementOf({trackWith(7, one, 0, 0.5f * gains.left, 0.5f * gains.right)}, {one});

    Mixer mixer;
    mixer.prepare(sampleRate, maximumBlock);
    const int rampLength = mixer.getGainRampLength();
    CHECK(rampLength == 960); // 20 ms

    auto state = MixState::startingAt(before);
    render(mixer, before, state, 0, 1000, {256});

    auto nextState = MixState::startingAt(after);
    std::size_t heapOperations = daw::test::heapOperationsDuring(
        [&] { nextState.continueFrom(state, before, after, rampLength); });
    CHECK(heapOperations == 0);

    const auto rendered = render(mixer, after, nextState, 1000, 2000, {256});
    CHECK(rendered.heapOperations == 0);

    for (int channel = 0; channel < 2; ++channel)
    {
        const float target = channel == 0 ? 0.5f * gains.left : 0.5f * gains.right;
        const float largestStep = std::abs(target - 1.0f) / static_cast<float>(rampLength);
        float previous = 1.0f;

        for (int n = 0; n < rampLength; ++n)
        {
            const float value = rendered.output.getSample(channel, n);
            REQUIRE(std::abs(value - previous) <= largestStep * 1.001f);
            previous = value;
        }

        // On target exactly at the end of the ramp, and from then on.
        for (int n = rampLength - 1; n < 2000; ++n)
            REQUIRE(rendered.output.getSample(channel, n) == target);
    }
}

TEST_CASE("Muting ramps a track down to silence", "[engine][mixer][ramp]")
{
    auto one = makeConstant(2, 48000, 1.0f);
    const auto playing = arrangementOf({trackWith(1, one, 0)}, {one});
    const auto muted = arrangementOf({trackWith(1, one, 0, 0.0f, 0.0f)}, {one});

    Mixer mixer;
    mixer.prepare(sampleRate, maximumBlock);
    auto state = MixState::startingAt(playing);
    auto mutedState = MixState::startingAt(muted);
    mutedState.continueFrom(state, playing, muted, mixer.getGainRampLength());

    const auto rendered = render(mixer, muted, mutedState, 0, 2000, {128});
    CHECK(rendered.output.getSample(0, 0) > 0.99f);
    CHECK(rendered.output.getSample(0, 479) == 0.5f); // halfway down the 960-sample ramp
    CHECK(rendered.output.getSample(0, mixer.getGainRampLength() - 1) == 0.0f);
    CHECK(rendered.output.getSample(1, 1999) == 0.0f);
}

TEST_CASE("Tracks keep their gains across arrangements by id, not by position",
          "[engine][mixer][ramp]")
{
    auto one = makeConstant(2, 48000, 1.0f);
    const auto first = arrangementOf(
        {trackWith(1, one, 0, 0.5f, 0.5f), trackWith(2, one, 0, 0.0f, 0.0f)}, {one});
    // Same tracks in the other order, plus a new one.
    const auto second = arrangementOf({trackWith(2, one, 0, 0.0f, 0.0f),
                                       trackWith(3, one, 0, 0.25f, 0.25f),
                                       trackWith(1, one, 0, 0.5f, 0.5f)},
                                      {one});

    Mixer mixer;
    mixer.prepare(sampleRate, maximumBlock);
    auto state = MixState::startingAt(first);
    auto nextState = MixState::startingAt(second);
    nextState.continueFrom(state, first, second, mixer.getGainRampLength());

    CHECK(nextState.tracks[0].left.getCurrent() == 0.0f); // track 2
    CHECK(nextState.tracks[1].left.getCurrent() == 0.25f); // new track 3: at its target
    CHECK(nextState.tracks[2].left.getCurrent() == 0.5f); // track 1
    CHECK_FALSE(nextState.tracks[2].left.isRamping());
}

TEST_CASE("The mix does not depend on how it is cut into blocks", "[engine][mixer]")
{
    auto a = makeNoise(2, 20000, 10);
    auto b = makeNoise(1, 15000, 11);
    auto c = makeRamp(30000);

    std::vector<ArrangementTrack> tracks;
    tracks.push_back(trackWith(1, a, 100, 0.7f, 0.3f));
    tracks.push_back(trackWith(2, b, 5003, 1.3f, 0.9f));

    ArrangementTrack cut;
    cut.id = 3;
    cut.leftGain = 0.01f;
    cut.rightGain = 0.02f;
    const std::vector<ClipPlacement> clips{
        {c, 0, 9000, 77}, {a, 4000, 4500, 3}, {c, 8000, 16000, 0}};
    cut.segments = daw::engine::flattenClips(clips, 240);
    tracks.push_back(cut);

    const auto before = arrangementOf(tracks, {a, b, c});
    for (auto& track : tracks)
    {
        track.leftGain *= 0.5f;
        track.rightGain *= 1.5f;
    }
    const auto after = arrangementOf(tracks, {a, b, c});

    // Ramping from "before" to "after" while rendering, so ramps cross block edges too.
    const auto renderWith = [&](const std::vector<int>& blockSizes) {
        Mixer mixer;
        mixer.prepare(sampleRate, maximumBlock);
        auto state = MixState::startingAt(before);
        auto rampingState = MixState::startingAt(after);
        rampingState.continueFrom(state, before, after, mixer.getGainRampLength());
        return render(mixer, after, rampingState, 0, 25000, blockSizes);
    };

    const auto reference = renderWith({4096});
    CHECK(reference.heapOperations == 0);

    for (const auto& sizes : std::vector<std::vector<int>>{{1}, {3}, {37}, {64}, {256}, {1000},
                                                           {13, 512, 1, 4096, 99}})
    {
        const auto rendered = renderWith(sizes);
        CHECK(rendered.heapOperations == 0);
        CHECK(bitIdentical(rendered.output, reference.output));
    }
}

TEST_CASE("Track meters measure each track after its fader", "[engine][mixer]")
{
    auto half = makeConstant(2, 1000, 0.5f);
    auto track = trackWith(1, half, 0, 0.5f, 1.0f);
    track.meter = std::make_shared<daw::engine::dsp::PeakLevelMeter>();
    const auto meter = track.meter;
    const auto arrangement = arrangementOf({track}, {half});

    renderFresh(arrangement, 0, 1000, {256});
    CHECK(meter->takePeak(0) == 0.25f);
    CHECK(meter->takePeak(1) == 0.5f);
}
