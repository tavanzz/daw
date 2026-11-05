#pragma once

// What the test plugins do, whatever their format (VST3, AU, LV2): small,
// deterministic and exact, so that tests can tell to the sample what the host gave
// them. No JUCE here: the plugins are built like third-party ones, against the
// formats' SDKs only.
//
// - gain: output = input * 2 * gain (normalised 0..1, 0.5 = unity exactly).
// - latency: output = input delayed by latencySamples, reported as its latency;
//   gain as above.
// - synth: an instrument whose output shows which notes it holds: left = the sum of
//   the held notes' velocities (0..1) times 2 * level, right = how many it holds.
//   Notes are matched by channel and key; a note-off for a note it does not hold
//   counts as a stray.
// - nan: passes its input through while "fail" is below 0.5, else outputs NaN.
// - crash and hang: crash (abort) or never return when scanned or loaded.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace daw::testplugins
{

enum class Kind
{
    gain,
    latency,
    synth,
    nan,
    crash,
    hang
};

inline constexpr int latencySamples = 777;

struct ParameterSpec
{
    const char* id;    // the symbol (LV2), the title (VST3, AU)
    const char* name;
    double defaultValue; // normalised
};

/** The one parameter each kind has (crash and hang have gain's). */
inline ParameterSpec parameterOf(Kind kind) noexcept
{
    switch (kind)
    {
    case Kind::synth:
        return {"level", "Level", 0.5};
    case Kind::nan:
        return {"fail", "Fail", 1.0};
    default:
        return {"gain", "Gain", 0.5};
    }
}

constexpr bool isInstrument(Kind kind) noexcept
{
    return kind == Kind::synth;
}

class Processor
{
public:
    explicit Processor(Kind processorKind) noexcept
        : kind(processorKind)
        , parameter(parameterOf(processorKind).defaultValue)
    {
    }

    /** Allocates; not real-time safe. */
    void prepare(double, int)
    {
        delay.assign(2 * static_cast<std::size_t>(latencySamples + 1), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill(delay.begin(), delay.end(), 0.0f);
        writePosition = 0;
        heldCount = 0;
    }

    [[nodiscard]] int getLatency() const noexcept { return kind == Kind::latency ? latencySamples : 0; }

    void setParameter(double normalised) noexcept { parameter = normalised; }
    [[nodiscard]] double getParameter() const noexcept { return parameter; }

    void noteOn(int channel, int key, float velocity) noexcept
    {
        if (velocity <= 0.0f)
        {
            noteOff(channel, key);
            return;
        }
        if (heldCount < static_cast<int>(held.size()))
            held[static_cast<std::size_t>(heldCount++)] = {channel, key, velocity};
    }

    void noteOff(int channel, int key) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[static_cast<std::size_t>(i)].channel == channel && held[static_cast<std::size_t>(i)].key == key)
            {
                for (int j = i + 1; j < heldCount; ++j)
                    held[static_cast<std::size_t>(j - 1)] = held[static_cast<std::size_t>(j)];
                --heldCount;
                return;
            }
        ++strays;
    }

    void allNotesOff() noexcept { heldCount = 0; }

    [[nodiscard]] int getHeldCount() const noexcept { return heldCount; }

    /** Processes [from, to) of both channels in place. */
    void process(float* left, float* right, int from, int to) noexcept
    {
        switch (kind)
        {
        case Kind::gain:
        case Kind::crash:
        case Kind::hang:
        {
            const auto gain = static_cast<float>(2.0 * parameter);
            for (int i = from; i < to; ++i)
            {
                left[i] *= gain;
                right[i] *= gain;
            }
            return;
        }
        case Kind::latency:
        {
            const auto gain = static_cast<float>(2.0 * parameter);
            const auto length = delay.size() / 2;
            for (int i = from; i < to; ++i)
            {
                const auto read = (writePosition + length - static_cast<std::size_t>(latencySamples)) % length;
                const float outLeft = delay[2 * read];
                const float outRight = delay[2 * read + 1];
                delay[2 * writePosition] = left[i];
                delay[2 * writePosition + 1] = right[i];
                writePosition = (writePosition + 1) % length;
                left[i] = outLeft * gain;
                right[i] = outRight * gain;
            }
            return;
        }
        case Kind::synth:
        {
            float velocities = 0.0f;
            for (int n = 0; n < heldCount; ++n)
                velocities += held[static_cast<std::size_t>(n)].velocity;
            const auto level = static_cast<float>(2.0 * parameter);
            for (int i = from; i < to; ++i)
            {
                left[i] = velocities * level;
                right[i] = static_cast<float>(heldCount);
            }
            return;
        }
        case Kind::nan:
            if (parameter >= 0.5)
                for (int i = from; i < to; ++i)
                    left[i] = right[i] = std::numeric_limits<float>::quiet_NaN();
            return;
        }
    }

    int strays = 0;

private:
    struct Note
    {
        int channel = 0;
        int key = 0;
        float velocity = 0.0f;
    };

    Kind kind;
    double parameter;
    std::vector<float> delay;
    std::size_t writePosition = 0;
    std::array<Note, 256> held{};
    int heldCount = 0;
};

/** The plugin's state: its parameter as 8 bytes, little-endian IEEE double. */
inline void writeState(double parameter, unsigned char* bytes) noexcept
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &parameter, sizeof bits);
    for (int i = 0; i < 8; ++i)
        bytes[i] = static_cast<unsigned char>((bits >> (8 * i)) & 0xff);
}

inline double readState(const unsigned char* bytes) noexcept
{
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    double parameter = 0.0;
    std::memcpy(&parameter, &bits, sizeof parameter);
    return parameter;
}

} // namespace daw::testplugins
