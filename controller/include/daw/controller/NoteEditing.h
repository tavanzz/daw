#pragma once

#include "daw/controller/ClipEditing.h"
#include "daw/model/Clip.h"
#include "daw/model/Grid.h"
#include "daw/model/MidiClip.h"
#include "daw/model/TempoMap.h"

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace daw::controller
{

/**
    The editing rules of MIDI notes and controller lanes, for every UI: the piano
    roll turns pointer positions into ticks and keys (PianoRollViewport) and calls
    these, then hands the result to Session::setNotes() (or setControllerEvents()).

    Notes are kept in their clip's content ticks; grids are on the timeline. A
    ClipFrame converts between the two. Functions taking a selection change the
    selected notes only, or every note when it is empty where they say so.
*/

using NoteIds = std::set<std::int32_t>;

/** Where a clip's content lies on the timeline: content tick c is at origin + c; the
    clip shows [start, end) of the timeline. */
struct ClipFrame
{
    model::Ticks origin = 0;
    model::Ticks start = 0;
    model::Ticks end = 0;

    [[nodiscard]] static ClipFrame of(const model::Clip& clip);

    [[nodiscard]] model::Ticks toTimeline(model::Ticks content) const noexcept { return origin + content; }
    [[nodiscard]] model::Ticks toContent(model::Ticks timeline) const noexcept { return timeline - origin; }
};

/** How to quantise: to which grid, how far (strength: 0 leaves notes where they
    are, 1 puts them on the grid) and whether their ends too. */
struct QuantiseSettings
{
    model::GridDivision division = model::GridDivision::sixteenth; ///< not automatic
    double strength = 1.0;
    bool ends = false;
};

/** Each note's start moves towards the nearest line of the grid by strength (to the
    nearest tick); with ends, its end too, and a note left with nothing between its
    start and end gets one grid step. The selected notes, or all when none is. */
[[nodiscard]] std::vector<model::Note> quantised(std::vector<model::Note> notes,
                                                 const NoteIds& selected, const ClipFrame& frame,
                                                 const model::TempoMap& tempoMap,
                                                 const QuantiseSettings& settings);

/** The selected notes (all when none is) moved by semitones; nothing if any of them
    would leave 0..127. */
[[nodiscard]] std::optional<std::vector<model::Note>>
transposed(std::vector<model::Note> notes, const NoteIds& selected, int semitones);

/** The result of dragging notes: the notes and which are selected after it (the
    copies, when copying). */
struct NoteDragResult
{
    std::vector<model::Note> notes;
    NoteIds selection;
};

/** The selected notes dragged by pointerDelta ticks and keyDelta keys, grabbed by
    one of them: its start snaps to the grid and the others follow it by the same
    amount; the keys move by keyDelta. Nothing goes before the start of the content or
    beyond 0..127. With copy, the originals stay and moved copies (new ids) are added. */
[[nodiscard]] NoteDragResult movedNotes(const std::vector<model::Note>& original,
                                        const NoteIds& selected, std::int32_t grabbed,
                                        model::Ticks pointerDelta, int keyDelta,
                                        const ClipFrame& frame, const Snapper& snap, bool copy);

/** The selected notes' ends dragged by pointerDelta, grabbed by one of them: its end
    snaps, the others' ends move by the same amount; every note keeps at least one
    tick (one grid step when snapping). */
[[nodiscard]] std::vector<model::Note> resizedEnds(const std::vector<model::Note>& original,
                                                   const NoteIds& selected, std::int32_t grabbed,
                                                   model::Ticks pointerDelta, const ClipFrame& frame,
                                                   const Snapper& snap);

/** The same for their starts, the ends staying put; never before the content's start. */
[[nodiscard]] std::vector<model::Note> resizedStarts(const std::vector<model::Note>& original,
                                                     const NoteIds& selected, std::int32_t grabbed,
                                                     model::Ticks pointerDelta, const ClipFrame& frame,
                                                     const Snapper& snap);

/** The selected notes (all when none is) at velocity. */
[[nodiscard]] std::vector<model::Note> withVelocity(std::vector<model::Note> notes,
                                                    const NoteIds& selected, int velocity);

/** Velocities drawn along a line from (from, fromVelocity) to (to, toVelocity) on
    the timeline: the notes starting in between (the selected ones only, if any are)
    take the line's value at their start. */
[[nodiscard]] std::vector<model::Note> velocitiesAlong(std::vector<model::Note> notes,
                                                       const NoteIds& selected,
                                                       const ClipFrame& frame, model::Ticks from,
                                                       int fromVelocity, model::Ticks to,
                                                       int toVelocity);

/** The notes in a rectangle of the timeline [from, to) and keys [lowKey, highKey]: a
    note is in it if it overlaps the time range and its key is in the key range. */
[[nodiscard]] NoteIds notesIn(const std::vector<model::Note>& notes, const ClipFrame& frame,
                              model::Ticks from, model::Ticks to, int lowKey, int highKey);

/** A note clicked into empty space at a timeline position: it starts on the grid
    line at or before it (where the click is when not snapping), lasts length. */
[[nodiscard]] model::Note newNoteAt(model::Ticks position, int key, model::Ticks length,
                                    int velocity, const ClipFrame& frame, const Snapper& snap);

enum class NotePart
{
    none,
    body,
    start, ///< near its start edge: resizes the start
    end    ///< near its end edge: resizes the end
};

struct NoteHit
{
    std::int32_t id = 0;
    NotePart part = NotePart::none;
};

/** The note at a timeline position on a key (the one starting last, if several), and
    whether the position is within edgeTicks of one of its edges (the end edge wins
    on short notes). */
[[nodiscard]] NoteHit hitNote(const std::vector<model::Note>& notes, const ClipFrame& frame,
                              model::Ticks position, int key, model::Ticks edgeTicks);

/** A controller's events (content ticks) with a straight line drawn from
    (from, fromValue) to (to, toValue) on the timeline: the events in between are
    replaced by one every spacing ticks where the value changes, and one at the end. */
[[nodiscard]] std::vector<model::ControllerEvent>
eventsAlongLine(std::vector<model::ControllerEvent> events, const model::ControllerType& type,
                const ClipFrame& frame, model::Ticks from, int fromValue, model::Ticks to,
                int toValue, model::Ticks spacing);

/** The index of the event nearest to (position, value) within the tolerances; -1 if
    none is. */
[[nodiscard]] int hitControllerEvent(const std::vector<model::ControllerEvent>& events,
                                     const ClipFrame& frame, model::Ticks position, int value,
                                     model::Ticks tickTolerance, int valueTolerance);

/** The value a controller has at a timeline position inside the clip: the last event
    at or before it, else its rest value. */
[[nodiscard]] int controllerValueAt(const std::vector<model::ControllerEvent>& events,
                                    const model::ControllerType& type, const ClipFrame& frame,
                                    model::Ticks position);

} // namespace daw::controller
