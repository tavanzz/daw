#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace daw::engine::midi
{

enum class EventType : std::uint8_t
{
    noteOn,
    noteOff,
    controller,     ///< a control change (CC): number is the controller, 0 to 127
    pitchBend,      ///< value from -1 (down) through 0 (centre) to just under 1 (up)
    channelPressure ///< aftertouch for the whole channel
};

/**
    One MIDI event at a sample of a block: what instruments receive, from the
    timeline, a device or the piano roll's preview.

    Values are normalised (velocities, controllers and pressure 0 to 1, pitch bend
    -1 to 1), as VST3 and CLAP take them, and convert exactly to and from MIDI 1.0's
    7 and 14 bits (see the functions below); MIDI 2.0's finer values fit too.
    noteId ties a note-on to its note-off (VST3 and CLAP note ids), so two notes on
    the same key can be told apart; -1 when it is not known.
*/
struct Event
{
    int offset = 0; ///< the sample of the block it happens at
    EventType type = EventType::noteOn;
    std::uint8_t channel = 0; ///< 0 to 15
    std::uint8_t number = 0;  ///< the key (notes) or the controller number
    float value = 0.0f;       ///< velocity (note-on: above 0), controller, pressure or bend
    std::int32_t noteId = -1;

    bool operator==(const Event&) const = default;
};

/** Controller numbers the app treats specially. */
namespace controllers
{
inline constexpr std::uint8_t modWheel = 1;
inline constexpr std::uint8_t volume = 7;
inline constexpr std::uint8_t pan = 10;
inline constexpr std::uint8_t expression = 11;
inline constexpr std::uint8_t sustain = 64;
inline constexpr std::uint8_t allSoundOff = 120;
inline constexpr std::uint8_t allNotesOff = 123;
} // namespace controllers

/** The value a controller rests at when nothing has moved it (and returns to at
    the end of a clip that moved it): pitch bend centred, volume (CC7) and expression
    (CC11) full, everything else 0. */
[[nodiscard]] constexpr float restValue(EventType type, int number) noexcept
{
    if (type == EventType::controller && (number == controllers::volume ||
                                          number == controllers::expression))
        return 1.0f;
    return 0.0f;
}

/** A 7-bit value (0 to 127), normalised; toSevenBit() gives it back exactly. */
[[nodiscard]] constexpr float fromSevenBit(int value) noexcept
{
    return static_cast<float>(std::clamp(value, 0, 127)) / 127.0f;
}

[[nodiscard]] inline int toSevenBit(float normalised) noexcept
{
    return static_cast<int>(std::lround(std::clamp(normalised, 0.0f, 1.0f) * 127.0f));
}

/** A 14-bit pitch bend (0 to 16383, 8192 the centre) as -1 to 1; toPitchBend()
    gives it back exactly. */
[[nodiscard]] constexpr float fromPitchBend(int value) noexcept
{
    return static_cast<float>(std::clamp(value, 0, 16383) - 8192) / 8192.0f;
}

[[nodiscard]] inline int toPitchBend(float bend) noexcept
{
    return std::clamp(static_cast<int>(std::lround(bend * 8192.0f)) + 8192, 0, 16383);
}

/** Order within one sample: note-offs first (a key struck again is released before
    it sounds anew), then controllers (a note starts with the right bend and
    sustain), then note-ons. */
[[nodiscard]] constexpr int orderOf(EventType type) noexcept
{
    switch (type)
    {
    case EventType::noteOff:
        return 0;
    case EventType::noteOn:
        return 2;
    case EventType::controller:
    case EventType::pitchBend:
    case EventType::channelPressure:
        break;
    }
    return 1;
}

/** Whether a comes before b in a block (see orderOf()). */
[[nodiscard]] constexpr bool comesBefore(const Event& a, const Event& b) noexcept
{
    return a.offset != b.offset ? a.offset < b.offset : orderOf(a.type) < orderOf(b.type);
}

[[nodiscard]] constexpr Event noteOn(int offset, int key, float velocity, int channel = 0,
                                     std::int32_t noteId = -1) noexcept
{
    return {offset, EventType::noteOn, static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(key), velocity, noteId};
}

[[nodiscard]] constexpr Event noteOff(int offset, int key, int channel = 0,
                                      std::int32_t noteId = -1, float velocity = 0.0f) noexcept
{
    return {offset, EventType::noteOff, static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(key), velocity, noteId};
}

[[nodiscard]] constexpr Event controller(int offset, int number, float value,
                                         int channel = 0) noexcept
{
    return {offset, EventType::controller, static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(number), value, -1};
}

[[nodiscard]] constexpr Event pitchBend(int offset, float bend, int channel = 0) noexcept
{
    return {offset, EventType::pitchBend, static_cast<std::uint8_t>(channel), 0, bend, -1};
}

/** A block's events, sorted (see comesBefore()). */
using EventSpan = std::span<const Event>;

} // namespace daw::engine::midi
