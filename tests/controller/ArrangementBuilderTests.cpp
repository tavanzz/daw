#include "daw/controller/ArrangementBuilder.h"
#include "daw/engine/OfflineRenderer.h"

#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <map>
#include <vector>

using daw::controller::buildArrangement;
using daw::controller::SourceLookup;
using daw::engine::ClipSegment;
using daw::model::ClipBounds;
using daw::model::Project;
using daw::model::Ticks;
using daw::model::ticksPerSecond;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr Ticks ticksPerSample = ticksPerSecond / 48000;

juce::File fileNamed(const juce::String& name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
}

// Sources by file name, as a SourcePool would give them.
struct FakeSources
{
    std::map<juce::String, std::shared_ptr<const daw::engine::MemoryAudioSource>> byName;

    void add(const juce::String& name, int length, float value, int channels = 2)
    {
        byName[name] = daw::test::makeConstantSource(channels, length, value, sampleRate);
    }

    [[nodiscard]] SourceLookup lookup() const
    {
        return [this](const juce::File& file) -> std::shared_ptr<const daw::engine::AudioSource> {
            const auto found = byName.find(file.getFileName());
            return found != byName.end() ? found->second : nullptr;
        };
    }
};

juce::AudioBuffer<float> render(const daw::engine::Arrangement& arrangement, int length)
{
    daw::engine::OfflineRenderer renderer(arrangement);
    juce::AudioBuffer<float> output(2, length);
    renderer.render(output);
    return output;
}

} // namespace

TEST_CASE("Clips become segments in samples at the arrangement's rate", "[controller][builder]")
{
    FakeSources sources;
    sources.add("a.wav", 48000, 0.5f);

    Project project;
    auto track = project.addTrack("T");
    track.addClip(fileNamed("a.wav"),
                  {100 * ticksPerSample, 10 * ticksPerSample, 500 * ticksPerSample});

    const auto arrangement = buildArrangement(project, sampleRate, sources.lookup());

    REQUIRE(arrangement->tracks.size() == 1);
    CHECK(arrangement->tracks[0].id == track.getId());
    REQUIRE(arrangement->tracks[0].segments.size() == 1);
    const auto& segment = arrangement->tracks[0].segments[0];
    CHECK(segment.start == 100);
    CHECK(segment.end == 600);
    CHECK(segment.sourceStart == 10);
    CHECK(segment.fadeInLength == 240);  // trimmed start
    CHECK(segment.fadeOutLength == 240); // trimmed end
    CHECK(arrangement->length == 600);
    CHECK(arrangement->sampleRate == sampleRate);
    CHECK(arrangement->sources.size() == 1);

    // The same project at 44.1 kHz: the same times in other samples.
    sources.add("a.wav", 44100, 0.5f);
    const auto at441 = buildArrangement(project, 44100.0, sources.lookup());
    CHECK(at441->tracks[0].segments[0].start ==
          daw::model::ticksToSamples(100 * ticksPerSample, 44100.0));
}

TEST_CASE("A whole file plays to its last sample at any rate", "[controller][builder]")
{
    // A 1.5 s file whose length in ticks came from another rate can land a sample
    // short of the end: it still plays to the end, with no fade.
    FakeSources sources;
    sources.add("a.wav", 66150, 0.5f); // 1.5 s at 44.1 kHz

    Project project;
    auto clip = project.addTrack("T").addClip(fileNamed("a.wav"));
    clip.resolveLength(daw::model::secondsToTicks(1.5) - ticksPerSecond / 44100 + 1);

    const auto arrangement = buildArrangement(project, 44100.0, sources.lookup());
    const auto& segment = arrangement->tracks[0].segments[0];
    CHECK(segment.end == 66150);
    CHECK(segment.fadeOutLength == 0);
}

TEST_CASE("Volume, pan, mute and solo become track gains", "[controller][builder]")
{
    Project project;
    auto a = project.addTrack("A");
    auto b = project.addTrack("B");
    auto c = project.addTrack("C");
    a.setVolume(0.5);
    b.setPan(-1.0);
    c.setMuted(true);
    project.setMasterVolume(0.25);

    auto arrangement = buildArrangement(project, sampleRate, {});
    CHECK(arrangement->tracks[0].leftGain == 0.5f);
    CHECK(arrangement->tracks[0].rightGain == 0.5f);
    CHECK(arrangement->tracks[1].leftGain == static_cast<float>(std::sqrt(2.0)));
    CHECK(arrangement->tracks[1].rightGain == 0.0f);
    CHECK(arrangement->tracks[2].leftGain == 0.0f);
    CHECK(arrangement->masterGain == 0.25f);

    b.setSoloed(true);
    arrangement = buildArrangement(project, sampleRate, {});
    CHECK(arrangement->tracks[0].leftGain == 0.0f);
    CHECK(arrangement->tracks[1].leftGain > 1.0f);
    CHECK(arrangement->tracks[2].leftGain == 0.0f);
}

TEST_CASE("Tracks sum, and mute and solo decide which are heard", "[controller][builder]")
{
    // Three tracks of constant values that cannot be confused in a sum.
    FakeSources sources;
    sources.add("one.wav", 4800, 1.0f / 8.0f);
    sources.add("two.wav", 4800, 1.0f / 4.0f);
    sources.add("four.wav", 4800, 1.0f / 2.0f, 1);

    Project project;
    project.addTrack("1").addClip(fileNamed("one.wav"));
    project.addTrack("2").addClip(fileNamed("two.wav"));
    project.addTrack("4").addClip(fileNamed("four.wav"));
    for (int t = 0; t < 3; ++t)
        project.getTrack(t).getClip(0).resolveLength(4800 * ticksPerSample);

    const auto heard = [&] {
        const auto output = render(*buildArrangement(project, sampleRate, sources.lookup()), 4800);
        CHECK(output.getSample(0, 1000) == output.getSample(1, 1000));
        return std::lround(output.getSample(0, 1000) * 8.0f); // 1, 2, 4 per track
    };

    CHECK(heard() == 7);

    project.getTrack(1).setMuted(true);
    CHECK(heard() == 5);

    project.getTrack(2).setSoloed(true);
    CHECK(heard() == 4);

    project.getTrack(1).setSoloed(true); // muted wins over solo
    CHECK(heard() == 4);

    project.getTrack(0).setSoloed(true);
    CHECK(heard() == 5);

    project.getTrack(1).setMuted(false);
    CHECK(heard() == 7);

    for (int t = 0; t < 3; ++t)
        project.getTrack(t).setSoloed(false);
    project.getTrack(0).setMuted(true);
    CHECK(heard() == 6);
}

TEST_CASE("Clips not loaded yet are left out or play silence", "[controller][builder]")
{
    FakeSources sources;
    sources.add("below.wav", 48000, 0.5f);

    Project project;
    auto track = project.addTrack("T");
    track.addClip(fileNamed("below.wav"), {0, 0, 1000 * ticksPerSample});
    track.addClip(fileNamed("missing.wav"), {200 * ticksPerSample, 0, 100 * ticksPerSample});
    track.addClip(fileNamed("unknown.wav")); // unknown length, not loaded

    const auto arrangement = buildArrangement(project, sampleRate, sources.lookup());
    const auto& segments = arrangement->tracks[0].segments;

    // The missing file still hides what is under it.
    REQUIRE(segments.size() == 2);
    CHECK(segments[0].end == 200);
    CHECK(segments[1].start == 300);
}

TEST_CASE("A clip of unknown length plays its whole file once loaded", "[controller][builder]")
{
    FakeSources sources;
    sources.add("a.wav", 3000, 0.5f);

    Project project;
    project.addTrack("T").addClip(fileNamed("a.wav"), {1000 * ticksPerSample, 0, std::nullopt});

    const auto arrangement = buildArrangement(project, sampleRate, sources.lookup());
    CHECK(arrangement->tracks[0].segments[0] ==
          ClipSegment{arrangement->sources[0].get(), 1000, 4000, 0, 0, 0});
}

TEST_CASE("The loop region and meters come along", "[controller][builder]")
{
    Project project;
    const auto id = project.addTrack("T").getId();
    project.setLoopRange(ticksPerSecond, 2 * ticksPerSecond);

    auto meter = std::make_shared<daw::engine::dsp::PeakLevelMeter>();
    const auto meters = [&](daw::model::ObjectId trackId) {
        return trackId == id ? meter : nullptr;
    };
    const auto arrangement = buildArrangement(project, sampleRate, {}, meters);

    CHECK(arrangement->loopStart == 48000);
    CHECK(arrangement->loopEnd == 96000);
    CHECK(arrangement->tracks[0].meter == meter);
    CHECK(arrangement->layoutHash != 0);
}
