#pragma once

#include "daw/model/ProjectChanges.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace daw::model
{

/**
    How a project is shared between the copies of a co-op session.

    Personal settings belong to each participant and never leave their copy: arming,
    the input and monitoring of a track, its MIDI input and solo; the metronome, the
    count-in, punch, the recording format, snap and the grid; the range ids are made
    in. The shared form of a project or of a change has none of them.

    File paths are absolute in memory and relative to the copy's folder in the shared
    form, so that every participant finds the files in their own copy.
*/

/** True for a property each participant keeps for themselves. */
[[nodiscard]] bool isPersonalProperty(const juce::Identifier& nodeType,
                                      const juce::Identifier& property);

/** True for a property that holds the path of a file. */
[[nodiscard]] bool isPathProperty(const juce::Identifier& nodeType,
                                  const juce::Identifier& property);

/** A copy of a project state (or of part of one) in the shared form: personal settings
    left out, file paths relative to folder (written with '/'). */
[[nodiscard]] juce::ValueTree sharedCopy(const juce::ValueTree& tree, const juce::File& folder);

/** A copy of a shared tree for a participant's copy in folder: absolute paths, and the
    default personal settings where it has none. */
[[nodiscard]] juce::ValueTree localCopy(const juce::ValueTree& sharedTree,
                                       const juce::File& folder);

/** The change in the shared form; nothing for a change of a personal setting. */
[[nodiscard]] std::optional<ProjectChange> sharedChange(const ProjectChange& change,
                                                        const juce::File& folder);

/** A change in the shared form made ready for a participant's copy in folder. */
[[nodiscard]] ProjectChange localChange(const ProjectChange& sharedChangeToConvert,
                                        const juce::File& folder);

/** Every change of changes in the shared form, personal settings left out. */
[[nodiscard]] ChangeSet sharedChanges(const ChangeSet& changes, const juce::File& folder);

/** Every change made ready for a participant's copy in folder. */
[[nodiscard]] ChangeSet localChanges(const ChangeSet& changes, const juce::File& folder);

/** Takes every personal setting out of tree and the nodes under it. */
void removePersonalSettings(juce::ValueTree& tree);

/** Gives every node of tree that lacks a personal setting its default (as a new track or
    project has it), without an undo history. */
void addDefaultPersonalSettings(juce::ValueTree& tree);

/** The ids of tracks, clips, buses, effects and sends one participant makes: participant
    n (from 1) gets [n * 2^40, (n + 1) * 2^40), so that two participants never make the
    same id. Ids made outside a session (or before it) are below 2^40. */
struct IdRange
{
    ObjectId first = 0;
    ObjectId last = 0;

    [[nodiscard]] bool contains(ObjectId id) const noexcept { return id >= first && id <= last; }
    bool operator==(const IdRange&) const = default;
};

[[nodiscard]] IdRange idRangeOfParticipant(int participantNumber) noexcept;

/** From now on new objects of the project whose root is projectState get ids in range
    (nothing: ids follow the largest one, as outside sessions). A personal setting. */
void setIdRange(juce::ValueTree projectState, std::optional<IdRange> range);
[[nodiscard]] std::optional<IdRange> getIdRange(const juce::ValueTree& projectState);

/** True if a node of tree (or tree itself) has this id. */
[[nodiscard]] bool containsId(const juce::ValueTree& tree, ObjectId id);

/** Every id of tree and of the nodes under it. */
[[nodiscard]] std::vector<ObjectId> idsIn(const juce::ValueTree& tree);

/**
    Whether a change keeps the project well formed: children of types their parent may
    have (a project has tracks, its buses and its master; a track clips and its
    containers; ...), containers once each and after the items, ids and lane targets
    where they belong. Checked by the server before it accepts a change from a
    participant; projectRoot is the state the change applies to.
*/
[[nodiscard]] juce::Result checkChange(const juce::ValueTree& projectRoot,
                                       const ProjectChange& change);

/** Whether a whole project state is well formed in the same sense, every id once. */
[[nodiscard]] juce::Result checkProject(const juce::ValueTree& projectRoot);

} // namespace daw::model
