#include "daw/engine/Engine.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/OfflineRenderer.h"
#include "daw/engine/dsp/PanLaw.h"

#include "support/Phase3Mixer.h"
#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;
using daw::engine::Arrangement;
using daw::engine::ArrangementTrack;
using daw::engine::AudioInputs;
using daw::engine::ChainSlot;
using daw::engine::ClipPlacement;
using daw::engine::EffectChain;
using daw::engine::Engine;
using daw::engine::InputMonitoring;
using daw::engine::MemoryAudioSource;
using daw::engine::MixContext;
using daw::engine::Mixer;
using daw::engine::MixState;
using daw::engine::OfflineRenderer;
using daw::engine::TimelineFade;
using daw::engine::fx::Effect;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::ProcessSetup;
using daw::test::BrokenEffect;
using daw::test::GainEffect;
using daw::test::LatencyEffect;
using daw::test::makeConstantSource;
using daw::test::makeWholeFileArrangement;
using daw::test::ParameterProbe;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int crossfadeLength = 240; // 5 ms
constexpr int effectFadeLength = 480; // 10 ms

std::shared_ptr<EffectInstance> host(std::unique_ptr<Effect> effect, bool fadeIn = false)
{
    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = 512;
    return std::make_shared<EffectInstance>(std::move(effect), setup, fadeIn);
}

ChainSlot slot(std::shared_ptr<EffectInstance> instance, bool enabled = true)
{
    ChainSlot chainSlot;
    chainSlot.instance = std::move(instance);
    chainSlot.enabled = enabled;
    return chainSlot;
}

// Every sample different, so that any misplaced or misweighted sample shows.
std::shared_ptr<const MemoryAudioSource> noise(int numChannels, int length, int seed)
{
    juce::AudioBuffer<float> samples(numChannels, length);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int n = 0; n < length; ++n)
            samples.setSample(channel, n,
                              0.5f * daw::test::testInputSample(channel + 4 * seed, n));
    return daw::test::makeSource(std::move(samples), sampleRate);
}

// A small song with everything the Phase 3 mixer did: overlapping clips with cut
// edges, mono and stereo files, pan, a muted track, a monitored input, the master.
std::unique_ptr<Arrangement> makeSong(bool variant)
{
    static const auto stereo = noise(2, 30000, 1);
    static const auto mono = noise(1, 20000, 2);
    static const auto other = noise(2, 12000, 3);

    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->sources = {stereo, mono, other};
    arrangement->masterGain = variant ? 1.2f : 0.7f;

    const auto addTrack = [&](juce::int64 id, std::vector<ClipPlacement> clips, float volume,
                              double pan) {
        ArrangementTrack track;
        track.id = id;
        const auto gains = daw::engine::dsp::panGains(pan);
        track.leftGain = static_cast<float>(volume * gains.left);
        track.rightGain = static_cast<float>(volume * gains.right);
        track.segments = daw::engine::flattenClips(clips, crossfadeLength);
        arrangement->tracks.push_back(std::move(track));
        return &arrangement->tracks.back();
    };

    addTrack(1, {{stereo, 1000, 25000, 500}, {other, 9000, 15000, 3000}}, variant ? 1.5f : 0.8f,
             -0.3);
    addTrack(2, {{mono, 0, 20000, 0}}, variant ? 0.0f : 1.0f, 0.0);
    addTrack(3, {{other, 4000, 16000, 0}}, variant ? 0.6f : 0.0f, 0.7);
    auto* monitored = addTrack(4, {{stereo, 20000, 30000, 0}}, 0.9f, 0.1);
    monitored->inputChannel = 0;
    monitored->numInputChannels = 2;
    monitored->monitoring = variant ? InputMonitoring::automatic : InputMonitoring::on;
    monitored->armed = true;

    arrangement->length = 30000;
    arrangement->updateLayoutHash();
    return arrangement;
}

enum class Chains
{
    none,
    bypassed,
    unity
};

// The same song with effects that must not change a bit: two bypassed gains, or two
// gains of exactly 1, on every track and on the master.
void addChains(Arrangement& arrangement, Chains chains)
{
    if (chains == Chains::none)
        return;

    const auto makeChain = [chains] {
        EffectChain chain;
        for (int i = 0; i < 2; ++i)
            chain.slots.push_back(slot(host(std::make_unique<GainEffect>(
                                           chains == Chains::unity ? 1.0f : 0.25f)),
                                       chains == Chains::unity));
        return chain;
    };

    for (auto& track : arrangement.tracks)
        track.inserts = makeChain();
    arrangement.masterInserts = makeChain();
}

bool sameBits(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    for (int channel = 0; channel < 2; ++channel)
        if (std::memcmp(a.getReadPointer(channel), b.getReadPointer(channel),
                        sizeof(float) * static_cast<std::size_t>(a.getNumSamples())) != 0)
            return false;
    return true;
}

struct Rendered
{
    std::vector<float> samples;
    std::size_t heapOperations = 0;
};

Rendered process(Engine& engine, int numBlocks)
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

float largestStep(const std::vector<float>& samples, std::size_t from = 0)
{
    float largest = 0.0f;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

std::unique_ptr<Arrangement> constantTrack(float value, juce::int64 id = 1, int length = 480000)
{
    return makeWholeFileArrangement(makeConstantSource(2, length, value, sampleRate), id);
}

// Two tracks in one arrangement: each a whole constant file.
std::unique_ptr<Arrangement> twoTracks(float first, float second)
{
    auto arrangement = constantTrack(first, 1);
    auto other = constantTrack(second, 2);
    arrangement->tracks.push_back(other->tracks.front());
    arrangement->sources.push_back(other->sources.front());
    arrangement->updateLayoutHash();
    return arrangement;
}

/** Multiplies by a fixed factor, in plain scalar code. */
class ScaleEffect final : public Effect
{
public:
    explicit ScaleEffect(float scale)
        : factor(scale)
    {
        description.type = "test-scale";
    }
    const daw::engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const ProcessSetup&) override {}
    void reset() noexcept override {}
    void process(const daw::engine::fx::ProcessBlock& block) noexcept override
    {
        for (int channel = 0; channel < block.numChannels; ++channel)
            for (int i = 0; i < block.numSamples; ++i)
                block.channels[channel][i] *= factor;
    }

private:
    daw::engine::fx::EffectDescription description;
    float factor = 1.0f;
};

/** Passes the audio on and remembers the largest input it saw. */
class InputSpy final : public Effect
{
public:
    InputSpy() { description.type = "test-spy"; }
    const daw::engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const ProcessSetup&) override {}
    void reset() noexcept override {}
    void process(const daw::engine::fx::ProcessBlock& block) noexcept override
    {
        for (int i = 0; i < block.numSamples; ++i)
            largest = std::max(largest, std::abs(block.channels[0][i]));
    }

    float largest = 0.0f;

private:
    daw::engine::fx::EffectDescription description;
};

} // namespace

TEST_CASE("Empty chains, bypassed effects and unity gains mix exactly like Phase 3",
          "[engine][fx][chain]")
{
    const auto chains = GENERATE(Chains::none, Chains::bypassed, Chains::unity);
    const auto pattern = GENERATE(std::vector<int>{256}, std::vector<int>{441},
                                  std::vector<int>{1, 7, 128, 4096, 33, 1000});

    auto first = makeSong(false);
    auto second = makeSong(true);
    addChains(*first, chains);
    addChains(*second, chains);

    Mixer mixer;
    mixer.prepare(sampleRate, 4096);
    daw::test::phase3::Mixer reference;
    reference.prepare(sampleRate, 4096);

    auto state = MixState::startingAt(*first);
    auto referenceState = daw::test::phase3::MixState::startingAt(*first);
    const Arrangement* playing = first.get();

    // 40000 samples: the first arrangement, then the second (its gains ramp from
    // the first's), the timeline fading out, stopped with the input monitored, then
    // fading in again.
    juce::int64 position = 0;
    int done = 0;
    std::size_t blockIndex = 0;
    bool identical = true;
    std::size_t heapOperations = 0;

    while (done < 40000)
    {
        // Blocks end where something changes, so that both mixers see the same
        // stretch with one context.
        int length = std::min(pattern[blockIndex++ % pattern.size()], 40000 - done);
        for (const int boundary : {15000, 24000, 25000, 32000, 33000})
            if (done < boundary)
                length = std::min(length, boundary - done);

        if (done >= 15000 && playing == first.get())
        {
            auto next = MixState::startingAt(*second);
            next.continueFrom(state, *first, *second, mixer.getGainRampLength());
            state = std::move(next);
            auto referenceNext = daw::test::phase3::MixState::startingAt(*second);
            referenceNext.continueFrom(referenceState, *first, *second,
                                       reference.getGainRampLength());
            referenceState = std::move(referenceNext);
            playing = second.get();
        }

        juce::AudioBuffer<float> inputs(2, length);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < length; ++i)
                inputs.setSample(channel, i, daw::test::testInputSample(channel, done + i));
        const float* const inputChannels[] = {inputs.getReadPointer(0), inputs.getReadPointer(1)};
        const AudioInputs audioInputs{inputChannels, 2};

        MixContext context;
        context.inputs = &audioInputs;
        context.timelineRunning = done < 25000 || done >= 32000;
        context.autoMonitorsInput = !context.timelineRunning;
        if (done >= 24000 && done < 25000)
            context.fade = {1000 - (done - 24000), -1, 1000};
        else if (done >= 32000 && done < 33000)
            context.fade = {done - 32000, 1, 1000};
        else if (!context.timelineRunning)
            context.fade = {0, 0, 1000};

        juce::AudioBuffer<float> mixed(2, length);
        juce::AudioBuffer<float> expected(2, length);
        heapOperations += daw::test::heapOperationsDuring(
            [&] { mixer.render(*playing, state, position, mixed, 0, length, context); });
        reference.render(*playing, referenceState, position, expected, 0, length, context);

        identical = identical && sameBits(mixed, expected);
        if (context.timelineRunning)
            position += length;
        done += length;
    }

    CAPTURE(static_cast<int>(chains), pattern);
    CHECK(identical);
    CHECK(heapOperations == 0);
}

TEST_CASE("A track's effects come before its fader, the master's after the master fader",
          "[engine][fx][chain]")
{
    auto trackEffect = std::make_unique<InputSpy>();
    auto masterEffect = std::make_unique<InputSpy>();
    auto* trackSpy = trackEffect.get();
    auto* masterSpy = masterEffect.get();

    auto arrangement = constantTrack(1.0f);
    arrangement->tracks[0].leftGain = 0.5f;
    arrangement->tracks[0].rightGain = 0.5f;
    arrangement->masterGain = 0.5f;
    arrangement->tracks[0].inserts.slots.push_back(slot(host(std::move(trackEffect))));
    arrangement->masterInserts.slots.push_back(slot(host(std::move(masterEffect))));

    Mixer mixer;
    mixer.prepare(sampleRate, 512);
    auto state = MixState::startingAt(*arrangement);
    juce::AudioBuffer<float> output(2, 512);
    mixer.render(*arrangement, state, 0, output, 0, 512);

    CHECK(trackSpy->largest == 1.0f);
    CHECK(masterSpy->largest == 0.25f);
    CHECK(output.getSample(0, 100) == 0.25f);

    // A muted track's effects still run (pre-fader); nothing of it is heard.
    arrangement->tracks[0].leftGain = 0.0f;
    arrangement->tracks[0].rightGain = 0.0f;
    state = MixState::startingAt(*arrangement);
    trackSpy->largest = 0.0f;
    mixer.render(*arrangement, state, 512, output, 0, 512);
    CHECK(trackSpy->largest == 1.0f);
    CHECK(output.getMagnitude(0, 512) == 0.0f);
}

TEST_CASE("Effects get the tempo, the metre and where the audio is on the timeline",
          "[engine][fx][chain]")
{
    auto probe = std::make_unique<ParameterProbe>(16);
    auto* spy = probe.get();
    auto arrangement = constantTrack(0.5f);
    arrangement->tempo = 90.0;
    arrangement->numerator = 6;
    arrangement->denominator = 8;
    arrangement->tracks[0].inserts.slots.push_back(slot(host(std::move(probe))));

    Mixer mixer;
    mixer.prepare(sampleRate, 512);
    auto state = MixState::startingAt(*arrangement);
    juce::AudioBuffer<float> output(2, 256);
    mixer.render(*arrangement, state, 12345, output, 0, 256);

    CHECK(spy->lastTime.position == 12345);
    CHECK(spy->lastTime.tempo == 90.0);
    CHECK(spy->lastTime.numerator == 6);
    CHECK(spy->lastTime.denominator == 8);
    CHECK(spy->lastTime.playing);

    MixContext stopped;
    stopped.timelineRunning = false;
    mixer.render(*arrangement, state, 12345, output, 0, 256, stopped);
    CHECK_FALSE(spy->lastTime.playing);
}

TEST_CASE("Effects ring on after the transport stops", "[engine][fx][chain]")
{
    // A 1000-sample clip through a pure 2000-sample delay: the transport stops at
    // the end of the clip, and the delayed clip still comes out afterwards.
    auto arrangement = makeWholeFileArrangement(makeConstantSource(2, 1000, 0.5f, sampleRate));
    arrangement->tracks[0].inserts.slots.push_back(slot(host(std::make_unique<LatencyEffect>(2000))));

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(std::move(arrangement));
    engine.play();
    const auto rendered = process(engine, 16);

    CHECK(engine.getTransportState() == daw::engine::TransportState::stopped);
    CHECK(rendered.heapOperations == 0);
    CHECK(rendered.samples[1999] == 0.0f);
    CHECK(rendered.samples[2000 + crossfadeLength] == 0.5f);
    CHECK(rendered.samples[2999] == 0.5f);
    CHECK(rendered.samples[3000] == 0.0f);
}

TEST_CASE("An effect added while playing fades in, and fades out when removed",
          "[engine][fx][chain]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    const auto source = makeConstantSource(2, 480000, 0.5f, sampleRate);
    engine.setArrangement(makeWholeFileArrangement(source));
    engine.play();
    (void) process(engine, 4);

    // Added: the effect (halving) is created on this thread and fades in on the
    // audio thread without any allocation there.
    const auto instance = host(std::make_unique<GainEffect>(0.5f), true);
    auto withEffect = makeWholeFileArrangement(source);
    withEffect->tracks[0].inserts.slots.push_back(slot(instance));
    engine.setArrangement(std::move(withEffect));
    const auto added = process(engine, 4);

    CHECK(added.heapOperations == 0);
    CHECK_THAT(added.samples[0], WithinAbs(0.5 - 0.25 / effectFadeLength, 1e-6));
    CHECK(added.samples[effectFadeLength - 1] == 0.25f);
    CHECK(added.samples.back() == 0.25f);
    CHECK(largestStep(added.samples) <= 0.25f / effectFadeLength * 1.001f);

    // Being removed: still there, switched off, it fades out.
    auto removing = makeWholeFileArrangement(source);
    removing->tracks[0].inserts.slots.push_back(slot(instance, false));
    engine.setArrangement(std::move(removing));
    const auto fadingOut = process(engine, 4);
    CHECK(fadingOut.heapOperations == 0);
    CHECK(fadingOut.samples[effectFadeLength - 1] == 0.5f);
    CHECK(largestStep(fadingOut.samples) <= 0.25f / effectFadeLength * 1.001f);

    // Gone: nothing changes.
    engine.setArrangement(makeWholeFileArrangement(source));
    const auto gone = process(engine, 2);
    CHECK(std::all_of(gone.samples.begin(), gone.samples.end(),
                      [](float sample) { return sample == 0.5f; }));
    engine.collectGarbage();
}

TEST_CASE("New clips while playing crossfade that track alone, before its effects",
          "[engine][fx][chain]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);

    auto start = twoTracks(0.25f, 0.125f);
    auto effect = std::make_unique<GainEffect>(2.0f);
    auto* counting = effect.get();
    const auto instance = host(std::move(effect));
    start->tracks[0].inserts.slots.push_back(slot(instance));
    auto next = std::make_unique<Arrangement>(*start);
    engine.setArrangement(std::move(start));
    engine.play();
    (void) process(engine, 4);

    // Track 2 plays another file: it crossfades from 0.125 to -0.125, while track 1
    // (through its effect, x2) goes on untouched.
    const auto replacement = makeConstantSource(2, 480000, -0.125f, sampleRate);
    next->tracks[1].segments.front().source = replacement.get();
    next->sources.push_back(replacement);
    next->updateLayoutHash();
    engine.setArrangement(std::move(next));
    const auto rendered = process(engine, 4);

    CHECK(rendered.heapOperations == 0);
    CHECK_THAT(rendered.samples[0], WithinAbs(0.5 + 0.125 - 0.25 / crossfadeLength, 1e-6));
    CHECK(rendered.samples[crossfadeLength - 1] == 0.375f);
    CHECK(rendered.samples.back() == 0.375f);
    CHECK(largestStep(rendered.samples) <= 0.25f / crossfadeLength * 1.001f);

    // The effect ran once per sample, never twice for the two arrangements.
    CHECK(counting->processed == 8 * blockSize);
}

TEST_CASE("A track removed while playing fades out, effects and all", "[engine][fx][chain]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    auto start = twoTracks(0.25f, 0.125f);
    start->tracks[1].inserts.slots.push_back(slot(host(std::make_unique<GainEffect>(2.0f))));
    auto remaining = std::make_unique<Arrangement>(*start);
    remaining->tracks.pop_back();
    remaining->updateLayoutHash();

    engine.setArrangement(std::move(start));
    engine.play();
    (void) process(engine, 4);

    engine.setArrangement(std::move(remaining));
    const auto rendered = process(engine, 4);

    CHECK(rendered.heapOperations == 0);
    CHECK_THAT(rendered.samples[0], WithinAbs(0.5 - 0.25 / crossfadeLength, 1e-6));
    CHECK(rendered.samples[crossfadeLength - 1] == 0.25f);
    CHECK(rendered.samples.back() == 0.25f);
    CHECK(largestStep(rendered.samples) <= 0.25f / crossfadeLength * 1.001f);
}

TEST_CASE("An effect that outputs NaN is silenced without touching the rest of the mix",
          "[engine][fx][chain]")
{
    auto arrangement = twoTracks(0.25f, 0.125f);
    const auto broken = host(std::make_unique<BrokenEffect>(300));
    arrangement->tracks[0].inserts.slots.push_back(slot(broken));

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(std::move(arrangement));
    engine.play();
    const auto rendered = process(engine, 8);

    // From the block where it broke (256 to 511), only the other track is heard.
    CHECK(rendered.samples[255] == 0.375f);
    CHECK(std::all_of(rendered.samples.begin() + 256, rendered.samples.end(),
                      [](float sample) { return sample == 0.125f; }));
    CHECK(broken->hasFailed());
    CHECK(rendered.heapOperations == 0);
}

TEST_CASE("Effects run with denormals flushed to zero on the audio and export threads",
          "[engine][fx]")
{
    // 1e-20 scaled by 1e-19 is 1e-39, below the smallest normal float: computed
    // with denormals on, it stays a denormal (and is slow on some processors).
    const auto source = makeConstantSource(2, 48000, 1.0e-20f, sampleRate);
    const auto withScaler = [&source] {
        auto arrangement = makeWholeFileArrangement(source);
        arrangement->tracks[0].inserts.slots.push_back(slot(host(std::make_unique<ScaleEffect>(1.0e-19f))));
        return arrangement;
    };

    // Without the audio thread's mode, here on the test thread: a denormal.
    ScaleEffect scaler(1.0e-19f);
    float sample = 1.0e-20f;
    float* const channels[] = {&sample};
    daw::engine::fx::ProcessBlock block;
    block.channels = channels;
    block.numChannels = 1;
    block.numSamples = 1;
    scaler.process(block);
    REQUIRE(std::fpclassify(sample) == FP_SUBNORMAL);

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(withScaler());
    engine.play();
    const auto played = process(engine, 8);
    CHECK(std::all_of(played.samples.begin(), played.samples.end(),
                      [](float value) { return value == 0.0f; }));

    const auto arrangement = withScaler();
    OfflineRenderer renderer(*arrangement);
    juce::AudioBuffer<float> exported(2, 4096);
    renderer.render(exported);
    bool allZero = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 4096; ++i)
            allZero = allZero && exported.getSample(channel, i) == 0.0f;
    CHECK(allZero);
}
