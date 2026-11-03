#include "daw/engine/fx/Parameter.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::fx
{

float ParameterInfo::clamp(float value) const noexcept
{
    if (!std::isfinite(value))
        return defaultValue;

    if (isChoice())
        return std::clamp(std::round(value), 0.0f, static_cast<float>(choices.size() - 1));

    return std::clamp(value, minimum, maximum);
}

float ParameterInfo::toNormalised(float value) const noexcept
{
    return ParameterMapping(*this).toNormalised(value);
}

float ParameterInfo::fromNormalised(float normalised) const noexcept
{
    return ParameterMapping(*this).toValue(normalised);
}

juce::String ParameterInfo::toText(float value) const
{
    value = clamp(value);

    if (isChoice())
        return choices[static_cast<int>(value)];

    // Kilo for large values in Hz and ms, so a frequency reads "1.20 kHz".
    if ((unit == "Hz" || unit == "ms") && std::abs(value) >= 1000.0f)
    {
        const auto scaled = value / 1000.0f;
        return juce::String(scaled, scaled >= 10.0f ? 1 : 2) + " " +
               (unit == "Hz" ? juce::String("kHz") : juce::String("s"));
    }

    const int decimals = std::abs(value) >= 100.0f ? 0 : (std::abs(value) >= 10.0f ? 1 : 2);
    const auto number = juce::String(value, decimals);
    return unit.isEmpty() ? number : number + " " + unit;
}

ParameterInfo choiceParameter(juce::String id, juce::String name, juce::StringArray choices,
                              int defaultChoice)
{
    jassert(!choices.isEmpty());
    ParameterInfo info;
    info.id = std::move(id);
    info.name = std::move(name);
    info.minimum = 0.0f;
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = static_cast<float>(juce::jlimit(0, choices.size() - 1, defaultChoice));
    info.choices = std::move(choices);
    return info;
}

ParameterInfo switchParameter(juce::String id, juce::String name, bool defaultOn)
{
    return choiceParameter(std::move(id), std::move(name), {"Off", "On"}, defaultOn ? 1 : 0);
}

ParameterMapping::ParameterMapping(const ParameterInfo& info) noexcept
    : scale(info.scale)
    , minimum(info.minimum)
    , maximum(info.maximum)
    , choiceCount(info.choices.size())
{
    if (scale == ParameterScale::logarithmic)
    {
        jassert(minimum > 0.0f && maximum > minimum);
        logMinimum = std::log(minimum);
        logRange = std::log(maximum) - logMinimum;
    }
}

float ParameterMapping::toValue(float normalised) const noexcept
{
    const float position = std::isfinite(normalised) ? std::clamp(normalised, 0.0f, 1.0f) : 0.0f;

    if (choiceCount > 0)
        return std::round(position * static_cast<float>(choiceCount - 1));

    if (scale == ParameterScale::logarithmic)
    {
        // The ends exactly, whatever exp() rounds to.
        if (position <= 0.0f)
            return minimum;
        if (position >= 1.0f)
            return maximum;
        return std::clamp(std::exp(logMinimum + position * logRange), minimum, maximum);
    }

    return minimum + (maximum - minimum) * position;
}

float ParameterMapping::toNormalised(float value) const noexcept
{
    if (!std::isfinite(value))
        return 0.0f;

    if (choiceCount > 0)
        return choiceCount > 1 ? std::clamp(std::round(value), 0.0f,
                                            static_cast<float>(choiceCount - 1)) /
                                     static_cast<float>(choiceCount - 1)
                               : 0.0f;

    if (maximum <= minimum)
        return 0.0f;

    if (scale == ParameterScale::logarithmic)
    {
        if (value <= minimum)
            return 0.0f;
        if (value >= maximum)
            return 1.0f;
        return std::clamp((std::log(value) - logMinimum) / logRange, 0.0f, 1.0f);
    }

    return std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
}

} // namespace daw::engine::fx
