#pragma once

#include "daw/model/Project.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <span>

namespace daw::model
{

/**
    Project files: human-readable JSON with a format marker and a format version.

        {
          "format": "daw-project",
          "formatVersion": 1,
          "project": { "loopEnabled": false, "tracks": [ ... ] }
        }

    - Audio files inside the project's folder are stored relative to it (with "/"
      separators), so the folder can be moved or copied as a whole; other audio
      files are stored as absolute paths.
    - Files from older format versions are upgraded through a chain of migrations,
      one per version step. Files from newer versions are refused with a message,
      never half-read. Unknown fields are ignored.

    Message thread or any background thread; never the audio thread.
*/

inline constexpr const char* projectFileExtension = ".dawproject";
inline constexpr const char* projectFormatMarker = "daw-project";

/** Outcome of loading a project. */
struct LoadedProject
{
    std::optional<Project> project; ///< empty on failure
    juce::String error;             ///< why loading failed, written for the user
    int fileFormatVersion = 0;      ///< the version stored in the file, 0 if unknown

    [[nodiscard]] bool succeeded() const noexcept { return project.has_value(); }
};

/** Upgrades the JSON of a project file from format version n to n + 1. */
using FormatMigration = juce::var (*)(juce::var json);

/** JSON text for the project, with paths relative to projectDirectory where possible. */
[[nodiscard]] juce::String toJson(const Project& project, const juce::File& projectDirectory);

/** Reads JSON text written by toJson() (or by an older version of it). Errors are
    phrased to follow "Could not open <file>: ". */
[[nodiscard]] LoadedProject fromJson(const juce::String& text, const juce::File& projectDirectory);

/** Writes the project to file. The old file is only replaced once the new one has
    been written completely, so a failure never leaves a broken project behind. */
[[nodiscard]] juce::Result saveProject(const Project& project, const juce::File& file);

[[nodiscard]] LoadedProject loadProject(const juce::File& file);

namespace detail
{
/** fromJson() with an explicit migration chain: migrations[i] upgrades version
    i + 1 to i + 2, so the newest readable version is migrations.size() + 1. */
[[nodiscard]] LoadedProject fromJson(const juce::String& text, const juce::File& projectDirectory,
                                     std::span<const FormatMigration> migrations);
} // namespace detail

} // namespace daw::model
