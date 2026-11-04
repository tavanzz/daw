#pragma once

#include "daw/engine/fx/Effect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace daw::test
{

/** Multiplies by its "gain" parameter and remembers its last output (state that
    reset() clears). Counts the samples it processed. */
class GainEffect final : public engine::fx::Effect
{
public:
    explicit GainEffect(float defaultGain = 1.0f)
    {
        engine::fx::ParameterInfo gain;
        gain.id = "gain";
        gain.name = "Gain";
        gain.minimum = -2.0f;
        gain.maximum = 2.0f;
        gain.defaultValue = defaultGain;
        description.type = "test-gain";
        description.name = "Test Gain";
        description.parameters.push_back(gain);
    }

    const engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const engine::fx::ProcessSetup&) override { reset(); }
    void reset() noexcept override
    {
        lastOutput = 0.0f;
        ++resets;
    }
    void process(const engine::fx::ProcessBlock& block) noexcept override
    {
        for (int channel = 0; channel < block.numChannels; ++channel)
            for (int i = 0; i < block.numSamples; ++i)
                block.channels[channel][i] *= block.parameters.at(0, i);
        if (block.numSamples > 0)
            lastOutput = block.channels[0][block.numSamples - 1];
        processed += block.numSamples;
    }

    float lastOutput = 0.0f;
    juce::int64 processed = 0;
    int resets = 0;

private:
    engine::fx::EffectDescription description;
};

/** A pure delay of a fixed number of samples, reported as its latency: the
    output is the input, latency samples later. */
class LatencyEffect final : public engine::fx::Effect
{
public:
    explicit LatencyEffect(int latencySamples)
        : latency(latencySamples)
    {
        description.type = "test-latency";
        description.name = "Test Latency";
    }

    const engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const engine::fx::ProcessSetup& setup) override
    {
        buffers.assign(static_cast<std::size_t>(setup.numChannels),
                       std::vector<float>(static_cast<std::size_t>(latency + 1), 0.0f));
        position = 0;
    }
    void reset() noexcept override
    {
        for (auto& buffer : buffers)
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        position = 0;
    }
    void process(const engine::fx::ProcessBlock& block) noexcept override
    {
        const auto length = static_cast<int>(latency + 1);
        for (int i = 0; i < block.numSamples; ++i)
        {
            for (int channel = 0; channel < block.numChannels; ++channel)
            {
                auto& buffer = buffers[static_cast<std::size_t>(channel)];
                buffer[static_cast<std::size_t>(position)] = block.channels[channel][i];
                block.channels[channel][i] =
                    buffer[static_cast<std::size_t>((position + 1) % length)];
            }
            position = (position + 1) % length;
        }
    }
    int getLatency() const noexcept override { return latency; }

private:
    engine::fx::EffectDescription description;
    int latency = 0;
    std::vector<std::vector<float>> buffers;
    int position = 0;
};

/** Passes the audio through until it has processed breakAt samples (since it was
    prepared or reset), then outputs NaN. */
class BrokenEffect final : public engine::fx::Effect
{
public:
    explicit BrokenEffect(juce::int64 breakAtSample, bool infinity = false)
        : breakAt(breakAtSample)
        , outputsInfinity(infinity)
    {
        description.type = "test-broken";
        description.name = "Test Broken";
    }

    const engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const engine::fx::ProcessSetup&) override { reset(); }
    void reset() noexcept override
    {
        processed = 0;
        ++resets;
    }
    void process(const engine::fx::ProcessBlock& block) noexcept override
    {
        const float bad = outputsInfinity ? std::numeric_limits<float>::infinity()
                                          : std::numeric_limits<float>::quiet_NaN();
        for (int i = 0; i < block.numSamples; ++i, ++processed)
            if (processed >= breakAt)
                for (int channel = 0; channel < block.numChannels; ++channel)
                    block.channels[channel][i] = bad;
    }

    int resets = 0;

private:
    engine::fx::EffectDescription description;
    juce::int64 breakAt = 0;
    bool outputsInfinity = false;
    juce::int64 processed = 0;
};

/** Writes the values it gets for each of its parameters, sample by sample, into
    preallocated memory (no allocation while processing). Its output is silence
    plus parameter 0 on both channels, so the values can also be heard. */
class ParameterProbe final : public engine::fx::Effect
{
public:
    explicit ParameterProbe(std::size_t capacity = 1 << 20)
    {
        engine::fx::ParameterInfo level;
        level.id = "level";
        level.name = "Level";
        level.minimum = 0.0f;
        level.maximum = 100.0f;
        level.defaultValue = 50.0f;

        engine::fx::ParameterInfo frequency;
        frequency.id = "frequency";
        frequency.name = "Frequency";
        frequency.unit = "Hz";
        frequency.minimum = 20.0f;
        frequency.maximum = 20000.0f;
        frequency.defaultValue = 1000.0f;
        frequency.scale = engine::fx::ParameterScale::logarithmic;

        description.type = "test-probe";
        description.name = "Test Probe";
        description.parameters = {level, frequency,
                                  engine::fx::choiceParameter("shape", "Shape",
                                                              {"Sine", "Square", "Saw"}, 0)};
        for (auto& values : recorded)
            values.reserve(capacity);
    }

    const engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const engine::fx::ProcessSetup&) override {}
    void reset() noexcept override {}
    void process(const engine::fx::ProcessBlock& block) noexcept override
    {
        for (int i = 0; i < block.numSamples; ++i)
        {
            for (std::size_t p = 0; p < recorded.size(); ++p)
                if (recorded[p].size() < recorded[p].capacity())
                    recorded[p].push_back(block.parameters.at(static_cast<int>(p), i));
            for (int channel = 0; channel < block.numChannels; ++channel)
                block.channels[channel][i] = block.parameters.at(0, i);
        }
        lastTime = block.time;
    }

    std::array<std::vector<float>, 3> recorded;
    engine::fx::TimeInfo lastTime;

private:
    engine::fx::EffectDescription description;
};

/**
    An instrument whose output shows exactly which notes it holds: from the sample of
    each event on, the left channel is the sum of the held notes' velocities (times
    its "level" parameter) and the right channel how many notes it holds. It records
    every event it gets with its sample (counted since it was prepared or reset), in
    preallocated memory, and complains (wellFormed) about a note-off for a note it
    does not hold.
*/
class TestInstrument final : public engine::fx::Effect
{
public:
    struct Received
    {
        juce::int64 sample = 0;
        engine::midi::Event event;
    };

    explicit TestInstrument(std::size_t capacity = 1 << 16)
    {
        engine::fx::ParameterInfo level;
        level.id = "level";
        level.name = "Level";
        level.minimum = 0.0f;
        level.maximum = 2.0f;
        level.defaultValue = 1.0f;
        description.type = "test-instrument";
        description.name = "Test Instrument";
        description.isInstrument = true;
        description.parameters.push_back(level);
        received.reserve(capacity);
    }

    const engine::fx::EffectDescription& getDescription() const noexcept override
    {
        return description;
    }
    void prepare(const engine::fx::ProcessSetup&) override { reset(); }
    void reset() noexcept override
    {
        heldCount = 0;
        processed = 0;
        ++resets;
    }
    void process(const engine::fx::ProcessBlock& block) noexcept override
    {
        std::size_t next = 0;
        for (int i = 0; i < block.numSamples; ++i, ++processed)
        {
            for (; next < block.events.size() && block.events[next].offset <= i; ++next)
                apply(block.events[next]);

            float velocities = 0.0f;
            for (int n = 0; n < heldCount; ++n)
                velocities += held[static_cast<std::size_t>(n)].value;
            block.channels[0][i] = outputsNaN ? std::numeric_limits<float>::quiet_NaN()
                                              : velocities * block.parameters.at(0, i);
            block.channels[1][i] = static_cast<float>(heldCount);
        }
        // Events past the block's end would be a host error: take them anyway.
        for (; next < block.events.size(); ++next)
        {
            wellFormed = false;
            apply(block.events[next]);
        }
    }

    [[nodiscard]] int getHeldCount() const noexcept { return heldCount; }

    std::vector<Received> received;
    int resets = 0;
    bool wellFormed = true;
    bool outputsNaN = false;

private:
    void apply(const engine::midi::Event& event) noexcept
    {
        if (received.size() < received.capacity())
            received.push_back({processed, event});

        if (event.type == engine::midi::EventType::noteOn)
        {
            if (heldCount < static_cast<int>(held.size()))
                held[static_cast<std::size_t>(heldCount++)] = event;
            return;
        }
        if (event.type != engine::midi::EventType::noteOff)
            return;

        for (int n = 0; n < heldCount; ++n)
        {
            const auto& note = held[static_cast<std::size_t>(n)];
            if (note.number == event.number && note.channel == event.channel &&
                (event.noteId < 0 || note.noteId == event.noteId))
            {
                for (int m = n + 1; m < heldCount; ++m)
                    held[static_cast<std::size_t>(m - 1)] = held[static_cast<std::size_t>(m)];
                --heldCount;
                return;
            }
        }
        wellFormed = false;
    }

    engine::fx::EffectDescription description;
    std::array<engine::midi::Event, 512> held{};
    int heldCount = 0;
    juce::int64 processed = 0;
};

} // namespace daw::test
