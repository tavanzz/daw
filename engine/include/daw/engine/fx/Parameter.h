#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

namespace daw::engine::fx
{

/** How a parameter's range is spread over 0..1, the position of its control and of
    its automation. */
enum class ParameterScale : std::uint8_t
{
    linear,     ///< evenly over the range (gains in dB, percentages)
    logarithmic ///< evenly in ratios (frequencies, times, Q); the range must be positive
};

/**
    Describes one parameter of an effect: what it is called, what values it takes
    and how they map onto 0..1 ("normalised", as controls and automation see it).

    Values themselves are in the parameter's own units (Hz, dB, ms...). A choice
    parameter takes the whole numbers 0 to choices.size() - 1; a switch is a choice
    of {"Off", "On"}.

    The id is what project files store: it never changes once released. The rest
    is for the user and may change.
*/
struct ParameterInfo
{
    juce::String id;
    juce::String name;
    juce::String unit; ///< "Hz", "dB", "ms", "%", ... or empty
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    ParameterScale scale = ParameterScale::linear;
    juce::StringArray choices; ///< non-empty for a choice parameter
    bool automatable = true;
    juce::String group; ///< the section of an editor it belongs to; empty for none
    /** Whether the host ramps the user's changes over 20 ms; plugins smooth their
        own, so their parameters jump (as choices do). */
    bool smoothed = true;

    [[nodiscard]] bool isChoice() const noexcept { return !choices.isEmpty(); }

    /** In range, and a whole number for a choice. */
    [[nodiscard]] float clamp(float value) const noexcept;

    /** 0 at minimum, 1 at maximum, following the scale. */
    [[nodiscard]] float toNormalised(float value) const noexcept;
    [[nodiscard]] float fromNormalised(float normalised) const noexcept;

    /** The value as the user reads it ("1.20 kHz", "-6.0 dB", "On"). Message thread. */
    [[nodiscard]] juce::String toText(float value) const;
};

/** A choice parameter. */
[[nodiscard]] ParameterInfo choiceParameter(juce::String id, juce::String name,
                                            juce::StringArray choices, int defaultChoice);

/** A switch: the choice {"Off", "On"}. */
[[nodiscard]] ParameterInfo switchParameter(juce::String id, juce::String name, bool defaultOn);

/**
    ParameterInfo's mapping, prepared for the audio thread: no strings, the
    logarithms worked out once.
*/
class ParameterMapping
{
public:
    ParameterMapping() = default;
    explicit ParameterMapping(const ParameterInfo& info) noexcept;

    /** Real-time safe. normalised is clamped to 0..1. */
    [[nodiscard]] float toValue(float normalised) const noexcept;
    [[nodiscard]] float toNormalised(float value) const noexcept;
    [[nodiscard]] bool isChoice() const noexcept { return choiceCount > 0; }

    /** Whether a change jumps instead of ramping (a choice, or not smoothed). */
    [[nodiscard]] bool jumps() const noexcept { return choiceCount > 0 || !smoothed; }

private:
    ParameterScale scale = ParameterScale::linear;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float logMinimum = 0.0f;
    float logRange = 0.0f;
    int choiceCount = 0;
    bool smoothed = true;
};

} // namespace daw::engine::fx
