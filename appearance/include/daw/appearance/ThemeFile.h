#pragma once

#include "daw/appearance/Theme.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace daw::appearance
{

/**
    Theme files: JSON describing one theme completely.

        {
          "id": "neon-grid",
          "name": "Neon Grid",
          "palette": { "background": "#07060B", "cyan": "#00F0FF", ... },
          "colours": { "window": "background", "outline": { "mix": ["line", "text", 0.6] },
                       "loopShade": { "alpha": ["violet", 0.1] }, ... },
          "clips": { "colours": ["cyan", ...], "bodyAlpha": 0.16, "headerAlpha": 0.95 },
          "fonts": { "title": { "family": "Chakra Petch", "style": "SemiBold", "size": 16,
                                "tracking": 0.08, "uppercase": true }, ... },
          "shape": { "corners": "rounded", "cornerSmall": 3, "cornerMedium": 4,
                     "cornerLarge": 6, "border": 1, "focus": 2, "glow": 10,
                     "labels": "plain", "statusBar": "plain", "pan": "knob" },
          "meter": { "style": "gradient", "stops": [ { "db": 0, "colour": "magenta" }, ... ] },
          "effects": { "cursorGlow": { "available": true, "cost": "low",
                                       "description": "The playhead pulses" }, ... },
          "summary": { "shapes": "...", "meters": "..." }
        }

    - The palette holds the theme's own colours ("#RRGGBB" or "#RRGGBBAA"), by name.
      Every other colour is a palette name, a hex colour, {"mix": [a, b, amount]}
      (a moved towards b, see Colour::mixedWith) or {"alpha": [colour, alpha]}, so
      derived colours show where they come from.
    - Every colour role, font role and effect must be present ("colours", "fonts" and
      "effects" are complete); segment meters also need "segmentHeight" and
      "segmentGap". Meter stops run from the loudest down.
    - Unlike project files, unknown fields are errors: a theme is written by hand and a
      misspelt key would otherwise leave a value silently unset.
*/

/** Outcome of reading a theme file. */
struct ThemeReadResult
{
    std::optional<Theme> theme; ///< empty if there is any error
    juce::StringArray errors;   ///< e.g. "colours.panel: missing"
};

[[nodiscard]] ThemeReadResult readTheme(const juce::String& json);

} // namespace daw::appearance
