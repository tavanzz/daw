#include "daw/appearance/ThemeLibrary.h"

#include "AppearanceResources.h"
#include "daw/appearance/ThemeFile.h"

#include <array>

namespace daw::appearance
{

namespace
{
constexpr std::array<const char*, 3> builtInThemeFileNames{"neon-grid.json", "acid-terminal.json",
                                                           "synth-chrome.json"};

juce::String embeddedText(const juce::String& fileName)
{
    namespace resources = daw::appearance::resources;

    for (int index = 0; index < resources::namedResourceListSize; ++index)
    {
        const auto* resourceName = resources::namedResourceList[index];

        if (fileName == resources::getNamedResourceOriginalFilename(resourceName))
        {
            int size = 0;
            const auto* data = resources::getNamedResource(resourceName, size);
            return juce::String::fromUTF8(data, size);
        }
    }

    jassertfalse; // listed above but not embedded: see appearance/CMakeLists.txt
    return {};
}

std::vector<Theme> readBuiltInThemes()
{
    std::vector<Theme> themes;

    for (const auto& file : builtInThemeFiles())
    {
        auto result = readTheme(file.json);

        if (result.theme)
        {
            themes.push_back(std::move(*result.theme));
        }
        else
        {
            juce::Logger::writeToLog("Theme " + file.fileName +
                                     " is damaged: " + result.errors.joinIntoString("; "));
            jassertfalse;
        }
    }

    return themes;
}
} // namespace

std::vector<ThemeFileData> builtInThemeFiles()
{
    std::vector<ThemeFileData> files;

    for (const auto* fileName : builtInThemeFileNames)
        files.push_back({fileName, embeddedText(fileName)});

    return files;
}

ThemeLibrary::ThemeLibrary(std::vector<Theme> themesToKeep) : themes(std::move(themesToKeep))
{
    jassert(!themes.empty());
}

const ThemeLibrary& ThemeLibrary::builtIn()
{
    static const ThemeLibrary library(readBuiltInThemes());
    return library;
}

const Theme* ThemeLibrary::find(const juce::String& id) const noexcept
{
    for (const auto& theme : themes)
        if (theme.id == id)
            return &theme;

    return nullptr;
}

const Theme& ThemeLibrary::getOrDefault(const juce::String& id) const noexcept
{
    if (const auto* theme = find(id))
        return *theme;

    return getDefault();
}

const Theme& ThemeLibrary::getDefault() const noexcept
{
    if (const auto* theme = find(defaultThemeId))
        return *theme;

    return themes.front();
}

} // namespace daw::appearance
