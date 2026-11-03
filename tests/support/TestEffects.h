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

} // namespace daw::test
