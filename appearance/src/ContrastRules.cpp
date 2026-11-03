#include "daw/appearance/ContrastRules.h"

#include "daw/appearance/ColourMetrics.h"

#include <array>
#include <cmath>
#include <vector>

namespace daw::appearance
{

namespace
{
using enum ColourRole;

constexpr ContrastKind textKind = ContrastKind::text;
constexpr ContrastKind graphicKind = ContrastKind::graphic;

// Surfaces that carry text.
constexpr std::array textSurfaces{window, panel, panelAlt, raised, inset, lane, laneAlt};

// Accents and messages shown as coloured text: values, status-bar messages.
constexpr std::array colouredText{accent, accentAlt, highlight, warning, error};
constexpr std::array colouredTextSurfaces{window, panel, raised, inset};

// Indicators, lines and fills that show state, and the surfaces they sit on.
constexpr std::array indicators{record,  mute,   solo,      clip,     automation,
                                monitor, accent, accentAlt, highlight};
constexpr std::array indicatorSurfaces{panel, lane, laneAlt};

constexpr std::array timelineMarks{playhead, selection, loop};
constexpr std::array timelineSurfaces{lane, laneAlt, panel, panelAlt};

constexpr std::array outlines{outline, focus};
constexpr std::array outlineSurfaces{window, panel, raised};

std::vector<ColourPair> makeColourPairs()
{
    std::vector<ColourPair> pairs;

    const auto add = [&pairs](const auto& foregrounds, const auto& backgrounds, ContrastKind kind)
    {
        for (const auto foreground : foregrounds)
            for (const auto background : backgrounds)
                pairs.push_back({foreground, background, kind});
    };

    add(std::array{text, textSecondary}, textSurfaces, textKind);
    add(colouredText, colouredTextSurfaces, textKind);
    add(indicators, indicatorSurfaces, graphicKind);
    add(timelineMarks, timelineSurfaces, graphicKind);
    add(outlines, outlineSurfaces, graphicKind);
    add(std::array{peakHold}, std::array{meterTrack}, graphicKind);
    add(std::array{curve, gainReduction}, std::array{inset, panel}, graphicKind);

    return pairs;
}

constexpr std::array filled{accent, accentAlt,  highlight, record,  mute, solo,
                            clip,   automation, monitor,   warning, error};

constexpr std::array statuses{record, mute, solo, clip, automation};

juce::String nameOf(ColourRole role)
{
    return juce::String(std::string(keyOf(role)));
}

juce::String ratioText(double ratio)
{
    return juce::String(ratio, 2) + ":1";
}

// Translucent colours are judged as they look over their surface, and surfaces over
// the window.
Colour opaque(Colour colour, Colour below)
{
    return colour.isOpaque() ? colour : colour.over(below);
}

void check(juce::StringArray& problems, const juce::String& what, Colour foreground,
           Colour background, double minimum)
{
    const double ratio = contrastRatio(foreground, background);

    if (ratio < minimum)
        problems.add(what + ": " + ratioText(ratio) + ", needs " + ratioText(minimum));
}
} // namespace

std::span<const ColourPair> colourPairs() noexcept
{
    static const std::vector<ColourPair> pairs = makeColourPairs();
    return pairs;
}

std::span<const ColourRole> filledRoles() noexcept
{
    return filled;
}

std::span<const ColourRole> statusRoles() noexcept
{
    return statuses;
}

ColourRole textRoleOn(const Theme& theme, Colour fill) noexcept
{
    const auto background = opaque(fill, theme.colour(window));
    return contrastRatio(theme.colour(textOnAccent), background) >=
                   contrastRatio(theme.colour(text), background)
               ? textOnAccent
               : text;
}

juce::StringArray contrastProblems(const Theme& theme)
{
    juce::StringArray problems;
    const auto windowColour = theme.colour(window);

    for (const auto& pair : colourPairs())
    {
        const auto background = opaque(theme.colour(pair.background), windowColour);
        const auto foreground = opaque(theme.colour(pair.foreground), background);
        check(problems, nameOf(pair.foreground) + " on " + nameOf(pair.background), foreground,
              background, minimumContrast(pair.kind));
    }

    for (const auto role : filled)
    {
        const auto fill = opaque(theme.colour(role), windowColour);
        const auto textRole = textRoleOn(theme, fill);
        check(problems, nameOf(textRole) + " on a " + nameOf(role) + " fill",
              theme.colour(textRole), fill, minimumContrast(ContrastKind::text));
    }

    // Clips: the name on the header strip, the waveform on the body, on either lane.
    for (std::size_t index = 0; index < theme.clips.colours.size(); ++index)
    {
        const auto colour = theme.clips.colours[index].withAlpha(1.0f);

        for (const auto laneRole : {lane, laneAlt})
        {
            const auto laneColour = opaque(theme.colour(laneRole), windowColour);
            const auto where = "clip colour " + juce::String(static_cast<int>(index) + 1) + " on " +
                               nameOf(laneRole);

            const auto header = colour.withAlpha(theme.clips.headerAlpha).over(laneColour);
            check(problems, where + ", name", theme.colour(textRoleOn(theme, header)), header,
                  minimumContrast(ContrastKind::text));

            const auto body = colour.withAlpha(theme.clips.bodyAlpha).over(laneColour);
            check(problems, where + ", waveform", colour, body,
                  minimumContrast(ContrastKind::graphic));
        }
    }

    // Meters: every level the bar can show, against the unlit track.
    const auto track = opaque(theme.colour(meterTrack), windowColour);
    for (int decibels = 0; decibels >= -60; --decibels)
    {
        const auto lit = opaque(theme.meter.colourAt(static_cast<float>(decibels)), track);
        check(problems, "meter at " + juce::String(decibels) + " dB", lit, track,
              minimumContrast(ContrastKind::graphic));
    }

    for (std::size_t first = 0; first < statuses.size(); ++first)
    {
        for (std::size_t second = first + 1; second < statuses.size(); ++second)
        {
            const double difference =
                std::abs(lightness(opaque(theme.colour(statuses[first]), windowColour)) -
                         lightness(opaque(theme.colour(statuses[second]), windowColour)));

            if (difference < minimumStatusLightnessDifference)
                problems.add(nameOf(statuses[first]) + " and " + nameOf(statuses[second]) +
                             ": L* " + juce::String(difference, 1) + " apart, needs " +
                             juce::String(minimumStatusLightnessDifference, 0));
        }
    }

    return problems;
}

} // namespace daw::appearance
