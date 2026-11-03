#pragma once

#include "daw/appearance/Theme.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace daw::appearance
{

/** The theme used when the user has not chosen one (or chose one this build lacks). */
inline constexpr const char* defaultThemeId = "neon-grid";

/** A built-in theme file: its name in appearance/themes and its JSON. */
struct ThemeFileData
{
    juce::String fileName;
    juce::String json;
};

/** The JSON of the built-in themes, in menu order (Neon Grid, Acid Terminal, Synth
    Chrome), as embedded in the app. */
[[nodiscard]] std::vector<ThemeFileData> builtInThemeFiles();

/** A set of themes in menu order, looked up by id. */
class ThemeLibrary
{
public:
    /** At least one theme. */
    explicit ThemeLibrary(std::vector<Theme> themesToKeep);

    /** The built-in themes, read once on first use (any thread). A built-in theme that
        does not read is left out; the tests make sure that never ships. */
    [[nodiscard]] static const ThemeLibrary& builtIn();

    [[nodiscard]] const std::vector<Theme>& getThemes() const noexcept { return themes; }

    /** The theme with this id, or nullptr. */
    [[nodiscard]] const Theme* find(const juce::String& id) const noexcept;

    /** The theme with this id, or the default theme: ids come from the user settings,
        which a newer build or a hand edit may have filled with one this build lacks. */
    [[nodiscard]] const Theme& getOrDefault(const juce::String& id) const noexcept;

    /** The default theme, or the first one if the library has no theme with that id. */
    [[nodiscard]] const Theme& getDefault() const noexcept;

private:
    std::vector<Theme> themes;
};

} // namespace daw::appearance
