#pragma once

#include "daw/appearance/Theme.h"

#include <juce_core/juce_core.h>

#include <span>

namespace daw::appearance
{

/** What is drawn in a colour, which sets the contrast it needs (WCAG 2: 1.4.3 for
    text, 1.4.11 for graphics). */
enum class ContrastKind
{
    text,      ///< at least 4.5:1
    largeText, ///< at least 3:1 (18 pt, or 14 pt bold, and larger)
    graphic    ///< at least 3:1: controls, indicators, lines that carry meaning
};

[[nodiscard]] constexpr double minimumContrast(ContrastKind kind) noexcept
{
    return kind == ContrastKind::text ? 4.5 : 3.0;
}

/** A colour drawn on another one. */
struct ColourPair
{
    ColourRole foreground;
    ColourRole background;
    ContrastKind kind;
};

/** Every foreground and background combination the user interfaces draw, apart from
    text on fills (see filledRoles()), clips and meters, which have rules of their own.
    A UI that draws another combination adds it here, so the tests check it in every
    theme. Decorative lines (separator, gridBar, gridBeat), translucent overlays and
    disabled text are exempt, as in WCAG. */
[[nodiscard]] std::span<const ColourPair> colourPairs() noexcept;

/** Colours used as fills that carry text: buttons that are on, status badges. */
[[nodiscard]] std::span<const ColourRole> filledRoles() noexcept;

/** The text colour for text on this fill: whichever of text and textOnAccent
    contrasts more with it. */
[[nodiscard]] ColourRole textRoleOn(const Theme& theme, Colour fill) noexcept;

/** Status colours, which must differ in lightness and not only in hue, so they tell
    apart for people who see hues differently and in greyscale. */
[[nodiscard]] std::span<const ColourRole> statusRoles() noexcept;

/** The smallest difference in CIELAB lightness (L*) between two status colours. */
inline constexpr double minimumStatusLightnessDifference = 10.0;

/** The smallest difference (colourDifference()) between two participants' colours, so
    that nobody's cursor or lock is taken for someone else's. */
inline constexpr double minimumParticipantDifference = 30.0;

/** Every way the theme breaks the contrast rules, e.g. "textSecondary on panel: 3.9:1,
    needs 4.5:1"; empty if it meets them all. */
[[nodiscard]] juce::StringArray contrastProblems(const Theme& theme);

} // namespace daw::appearance
