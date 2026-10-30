#pragma once

#include "daw/model/TimelineTime.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace daw::model
{

/** Identifies a track or a clip within its project; never 0. */
using ObjectId = juce::int64;

/** Where a clip sits on the timeline and which part of its file it plays. */
struct ClipBounds
{
    Ticks position = 0; ///< timeline position of the clip's first sample
    Ticks offset = 0;   ///< how far into the file the clip starts
    /** How much of the file it plays; empty means "up to the end of the file", for
        clips whose file has not been read yet (see Clip::resolveLength()). */
    std::optional<Ticks> length;

    bool operator==(const ClipBounds&) const = default;
};

/**
    A part of an audio file placed on a track's timeline.

    A lightweight handle onto part of a Project's state: copies refer to the same
    clip, and changes go straight into the project. Changes that take an
    UndoManager are undoable when one is passed.
*/
class Clip
{
public:
    /** The shortest clip that can be made by editing. */
    static constexpr Ticks minimumLength = 1;

    explicit Clip(juce::ValueTree clipState);

    [[nodiscard]] ObjectId getId() const;

    /** The audio file, as an absolute path (project files store it relative to the
        project when they can). */
    [[nodiscard]] juce::File getAudioFile() const;
    void setAudioFile(const juce::File& file, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] ClipBounds getBounds() const;
    [[nodiscard]] Ticks getPosition() const;
    [[nodiscard]] Ticks getOffset() const;
    [[nodiscard]] std::optional<Ticks> getLength() const;

    /** position + length, or empty while the length is unknown. */
    [[nodiscard]] std::optional<Ticks> getEnd() const;

    /** Position and offset are clamped to 0 or more, a length to minimumLength or more. */
    void setBounds(const ClipBounds& bounds, juce::UndoManager* undoManager = nullptr);
    void setPosition(Ticks position, juce::UndoManager* undoManager = nullptr);

    /** Fills in an unknown length once the file has been read: what is left of the
        file after the offset. Not an edit, so never undoable; does nothing if the
        length is already known. */
    void resolveLength(Ticks fileLength);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
