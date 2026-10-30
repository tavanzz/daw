#pragma once

#include "daw/model/Grid.h"
#include "daw/model/TempoMap.h"
#include "daw/model/Track.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace daw::model
{

/** Where a clip is: its track and its index on that track. */
struct ClipLocation
{
    int trackIndex = -1;
    int clipIndex = -1;
};

/**
    Everything a project file stores: tempo and metre, loop region, snap settings,
    master volume and the tracks with their clips.

    The state is a juce::ValueTree, so changes can be undone (pass an UndoManager)
    and watched (ValueTree::Listener). What is undoable is decided here, by which
    setters take an UndoManager: loop on/off, snap and grid are settings the user
    flips while working, saved with the project but not part of its undo history.

    Message thread only; the engine never sees the model directly.
*/
class Project
{
public:
    /** An empty project: no tracks, 120 BPM in 4/4, loop off over the whole project,
        snap on with the automatic grid, master at 0 dB. */
    Project();

    /** Wraps existing project state, as built by ProjectFile. */
    explicit Project(juce::ValueTree projectState);

    // Tracks
    [[nodiscard]] int getNumTracks() const;
    [[nodiscard]] Track getTrack(int index) const;
    [[nodiscard]] int indexOfTrack(ObjectId trackId) const; ///< -1 if there is none
    Track addTrack(const juce::String& name, juce::UndoManager* undoManager = nullptr);
    void removeTrack(int index, juce::UndoManager* undoManager = nullptr);

    /** Track mute and solo together: a track is heard if it is not muted and either
        no track is soloed or it is soloed itself. */
    [[nodiscard]] bool isAnyTrackSoloed() const;
    [[nodiscard]] bool isTrackAudible(int index) const;

    // Clips
    [[nodiscard]] std::optional<ClipLocation> findClip(ObjectId clipId) const;

    /** Puts a clip at a new position on a track (the same one or another), on top
        of the clips there. Keeps its id. */
    void moveClip(ObjectId clipId, int toTrackIndex, Ticks newPosition,
                  juce::UndoManager* undoManager = nullptr);
    void removeClip(ObjectId clipId, juce::UndoManager* undoManager = nullptr);

    /** Where the last clip ends (0 if there are none). Clips of unknown length count
        up to their start. */
    [[nodiscard]] Ticks getEnd() const;

    // Tempo and metre
    [[nodiscard]] TempoMap getTempoMap() const;
    void setTempo(double quarterNotesPerMinute, juce::UndoManager* undoManager = nullptr);
    /** Invalid time signatures are ignored. */
    void setTimeSignature(TimeSignature timeSignature, juce::UndoManager* undoManager = nullptr);

    // Loop
    [[nodiscard]] bool isLoopEnabled() const;
    void setLoopEnabled(bool shouldLoop);

    /** The loop region [start, end). An empty region (start == end) loops the whole
        project, from 0 to getEnd(). */
    [[nodiscard]] Ticks getLoopStart() const;
    [[nodiscard]] Ticks getLoopEnd() const;
    /** Swapped if end < start; negative times become 0. */
    void setLoopRange(Ticks start, Ticks end, juce::UndoManager* undoManager = nullptr);

    // Snap
    [[nodiscard]] bool isSnapEnabled() const;
    void setSnapEnabled(bool shouldSnap);
    [[nodiscard]] GridDivision getGridDivision() const;
    void setGridDivision(GridDivision division);

    // Master bus
    [[nodiscard]] double getMasterVolume() const; ///< linear gain, as Track::getVolume()
    void setMasterVolume(double gain, juce::UndoManager* undoManager = nullptr);

    /** Same settings, tracks and clips, in the same order. */
    [[nodiscard]] bool isEquivalentTo(const Project& other) const;

    [[nodiscard]] juce::ValueTree& getState() noexcept { return state; }
    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
