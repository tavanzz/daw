#pragma once

#include "daw/appearance/AnimationEffect.h"
#include "daw/appearance/Colour.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace daw::appearance
{

/** What a colour is used for. Every theme gives every role a colour; components ask
    for roles, never for literal colours. */
enum class ColourRole
{
    window,        ///< behind everything
    panel,         ///< transport, track headers, mixer strips, dialogs
    panelAlt,      ///< secondary panels: ruler, groups inside a panel
    raised,        ///< buttons, popup menus, tooltips
    inset,         ///< meter wells, text fields, displays, editor graphs
    lane,          ///< even track lanes on the timeline
    laneAlt,       ///< odd track lanes
    separator,     ///< lines between areas (decorative)
    gridBar,       ///< bar lines on the timeline (decorative)
    gridBeat,      ///< beat and subdivision lines (decorative)
    outline,       ///< borders of controls, which show their extent
    text,          ///< primary text
    textSecondary, ///< labels, units, secondary information
    textDisabled,  ///< text of controls that are switched off
    textOnAccent,  ///< dark text on bright fills (accents and status colours)
    accent,        ///< active controls, fader and knob fills
    accentAlt,     ///< a second accent: pan, sends
    highlight,     ///< a third accent: tempo, markers
    focus,         ///< keyboard focus outline
    selection,     ///< outline of selected clips and points
    playhead,      ///< the playhead line
    loop,          ///< the loop region in the ruler
    loopShade,     ///< the loop region over the lanes (translucent)
    record,        ///< arming, recording, the Rec button
    mute,          ///< muted tracks
    solo,          ///< soloed tracks
    clip,          ///< meter clip indicators, overloads
    automation,    ///< automation curves and active automation
    monitor,       ///< input monitoring
    warning,       ///< warnings in the status bar
    error,         ///< errors in the status bar
    meterTrack,    ///< the unlit part of meters
    peakHold,      ///< the peak-hold line of meters
    curve,         ///< response curves in effect editors
    curveFill,     ///< the area under a response curve (translucent)
    gainReduction, ///< compressor and limiter gain reduction
    shadow,        ///< drop shadows (translucent)
    keyWhite,      ///< white keys of the piano roll's keyboard
    keyBlack,      ///< black keys
    keyRow,        ///< the piano roll's rows of black keys, behind the notes
    count
};

inline constexpr std::size_t colourRoleCount = static_cast<std::size_t>(ColourRole::count);

/** Where a font is used. */
enum class FontRole
{
    title,   ///< the logo, panel and window titles
    heading, ///< section headings in panels and dialogs
    label,   ///< buttons, labels, menus (not "interface" nor "small": Windows macros)
    caption, ///< secondary labels, ruler numbers
    numeric, ///< values: levels, frequencies, times in lists
    display, ///< the transport's time display
    count
};

inline constexpr std::size_t fontRoleCount = static_cast<std::size_t>(FontRole::count);

/** The role's key in theme files, e.g. "textSecondary". */
[[nodiscard]] std::string_view keyOf(ColourRole role) noexcept;
[[nodiscard]] std::string_view keyOf(FontRole role) noexcept;

struct FontSpec
{
    juce::String family;    ///< one of the bundled families, e.g. "Chakra Petch"
    juce::String style;     ///< the face within the family, e.g. "SemiBold"
    float size = 0.0f;      ///< height in logical pixels
    float tracking = 0.0f;  ///< extra space between letters, as a fraction of the size
    bool uppercase = false; ///< text in this role is shown in capitals
};

enum class CornerStyle
{
    rounded, ///< corner sizes are radii
    sharp,   ///< square corners; corner sizes are 0
    cut      ///< corners cut at 45 degrees; corner sizes are the length cut off
};

enum class LabelStyle
{
    plain,
    terminal ///< labels look like terminal output, e.g. "[MUTE]"
};

enum class StatusBarStyle
{
    plain,
    prompt ///< the status bar reads like a shell prompt with a cursor
};

enum class PanStyle
{
    knob, ///< a rotary knob
    arc   ///< an arc lit from the centre towards the pan position
};

struct Shape
{
    CornerStyle corners = CornerStyle::rounded;
    float cornerSmall = 0.0f;  ///< small controls: buttons, slots
    float cornerMedium = 0.0f; ///< clips, fields, menus
    float cornerLarge = 0.0f;  ///< panels and dialogs
    float border = 1.0f;       ///< control borders, in pixels
    float focus = 2.0f;        ///< the focus outline, in pixels
    float glow = 0.0f;         ///< radius of the glow around active elements; 0 for none
    LabelStyle labels = LabelStyle::plain;
    StatusBarStyle statusBar = StatusBarStyle::plain;
    PanStyle pan = PanStyle::knob;
};

enum class MeterStyle
{
    gradient, ///< a continuous bar coloured by a gradient through the stops
    segments  ///< LED segments, each lit in the colour of its zone
};

struct MeterStop
{
    float decibels = 0.0f; ///< 0 dB or below
    Colour colour;
};

struct MeterLook
{
    MeterStyle style = MeterStyle::gradient;
    std::vector<MeterStop> stops; ///< from the loudest down, at least two
    float segmentHeight = 0.0f;   ///< segment meters only, in pixels
    float segmentGap = 0.0f;      ///< segment meters only, in pixels

    /** The colour of the meter at this level: interpolated between the stops for a
        gradient, the colour of the zone (the loudest stop at or below the level) for
        segments. Below the last stop, its colour. */
    [[nodiscard]] Colour colourAt(float decibels) const noexcept;
};

/** How costly an effect is to draw, shown next to its switch. */
enum class Cost
{
    low,
    medium,
    high
};

struct EffectLook
{
    bool available = false; ///< the theme draws this effect
    Cost cost = Cost::low;
    juce::String description; ///< what the effect looks like in this theme
};

struct ClipLook
{
    std::vector<Colour> colours; ///< taken in turn by the tracks
    float bodyAlpha = 0.0f;      ///< opacity of the clip's colour over its lane
    float headerAlpha = 0.0f;    ///< opacity of the name strip over its lane

    /** The colour of the clips of the track at this index. */
    [[nodiscard]] Colour colourFor(int trackIndex) const noexcept;
};

/** How many participants of a co-op session have a colour of their own (the server gives
    each one of these, coop::ParticipantInfo::colour). */
inline constexpr std::size_t participantColourCount = 8;

/** Everything a user interface needs to draw in one theme. Read from the JSON files
    in appearance/themes (see ThemeFile.h). */
struct Theme
{
    juce::String id;   ///< stable, kept in the user settings, e.g. "neon-grid"
    juce::String name; ///< shown to the user, e.g. "Neon Grid"
    std::array<Colour, colourRoleCount> colours{};
    std::array<FontSpec, fontRoleCount> fonts{};
    Shape shape;
    MeterLook meter;
    ClipLook clips;
    /** Co-op participants: their cursors, playheads, selections, locks and names. */
    std::array<Colour, participantColourCount> participants{};
    std::array<EffectLook, animationEffectCount> effects{};
    juce::String shapeSummary; ///< for the theme previews
    juce::String meterSummary; ///< for the theme previews

    [[nodiscard]] Colour colour(ColourRole role) const noexcept;
    [[nodiscard]] const FontSpec& font(FontRole role) const noexcept;
    [[nodiscard]] const EffectLook& effect(AnimationEffect animationEffect) const noexcept;
    /** The colour of a participant's index (any int: taken in turn). */
    [[nodiscard]] Colour participantColour(int index) const noexcept;

    /** The font families in role order, each once, e.g. "Chakra Petch, JetBrains Mono". */
    [[nodiscard]] juce::String fontSummary() const;
};

} // namespace daw::appearance
