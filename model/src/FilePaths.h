#pragma once

#include <juce_core/juce_core.h>

namespace daw::model::detail
{

/** How a project stores a file: relative to its folder (with '/') when the file is in it,
    else the full path. An empty path for no file. */
[[nodiscard]] inline juce::String pathForFile(const juce::File& file,
                                              const juce::File& projectDirectory)
{
    if (file == juce::File())
        return {};

    if (file.isAChildOf(projectDirectory))
        return file.getRelativePathFrom(projectDirectory).replaceCharacter('\\', '/');

    return file.getFullPathName();
}

/** The file a stored path names, relative paths from the project's folder. */
[[nodiscard]] inline juce::File fileForPath(const juce::String& path,
                                            const juce::File& projectDirectory)
{
    if (path.isEmpty())
        return {};

    if (juce::File::isAbsolutePath(path))
        return juce::File(path);

    return projectDirectory.getChildFile(path);
}

} // namespace daw::model::detail
