#pragma once

#include "daw/model/TimelineTime.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace daw::controller
{

/** A recording the saved project does not have yet. */
struct JournalEntry
{
    juce::File file;
    juce::int64 trackId = 0;
    model::Ticks firstPosition = 0; ///< where the file's first sample belongs
    model::Ticks keepStart = 0;     ///< the part to make a clip of...
    std::optional<model::Ticks> keepEnd; ///< ...to here; unknown while it records

    bool operator==(const JournalEntry&) const = default;
};

/**
    The project's recordings that are not in its saved file yet, kept on disk next to
    them (`Audio/.recording-journal.json`) so that they can be recovered when the
    project is opened after a crash. Written when a recording starts and ends,
    removed when the project is saved. Files are stored relative to the project.

    Any thread except the audio thread.
*/
class RecordingJournal
{
public:
    [[nodiscard]] static juce::File fileFor(const juce::File& projectDirectory);

    /** The entries of the project's journal; none if it has none or it is damaged. */
    [[nodiscard]] static std::vector<JournalEntry> read(const juce::File& projectDirectory);

    /** Replaces the journal (deletes it when entries is empty); false on failure. */
    static bool write(const juce::File& projectDirectory, const std::vector<JournalEntry>& entries);
};

} // namespace daw::controller
