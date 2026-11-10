#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

namespace daw::model
{

/**
    One step from a node of the project state to one of its children: the child's type
    and how it differs from its siblings of that type. Tracks, clips, buses, effects and
    sends are found by their id, automation lanes by their target, anything else
    (containers, automation points) by its index among the siblings of its type.

    Ids keep a path valid whatever happens around the node; indices are only used
    where one person at a time changes the siblings (a container is unique, the points
    of a lane belong to one track).
*/
struct PathStep
{
    enum class Key : std::uint8_t
    {
        id,
        target,
        index
    };

    juce::Identifier type;
    Key key = Key::index;
    ObjectId id = 0;
    juce::String target;
    int index = 0;

    bool operator==(const PathStep&) const = default;
};

/** The steps from the project's root to a node; empty for the root itself. */
using NodePath = std::vector<PathStep>;

/** How node is found from the root of the tree it belongs to. */
[[nodiscard]] NodePath pathOf(const juce::ValueTree& node);

/** The node at path under root; an invalid tree if there is none. */
[[nodiscard]] juce::ValueTree findNode(const juce::ValueTree& root, const NodePath& path);

/**
    One change of the project state, with what was there before, so that it can be
    checked, inverted and applied elsewhere.

    - setProperty: path is the node; before and after are the property's values, empty
      when the node has no such property.
    - insertChild: path is the parent; child (a copy) went in at index.
    - removeChild: path is the parent; child (a copy, as it was) left from index.
    - moveChild: path is the parent; the child at index went to newIndex.
*/
struct ProjectChange
{
    enum class Kind : std::uint8_t
    {
        setProperty,
        insertChild,
        removeChild,
        moveChild
    };

    Kind kind = Kind::setProperty;
    NodePath path;
    juce::Identifier property;
    std::optional<juce::var> before;
    std::optional<juce::var> after;
    int index = 0;
    int newIndex = 0;
    juce::ValueTree child;
};

using ChangeSet = std::vector<ProjectChange>;

/** The changes that undo these: each one inverted, in reverse order. */
[[nodiscard]] ChangeSet inverted(const ChangeSet& changes);
[[nodiscard]] ProjectChange inverted(const ProjectChange& change);

/**
    Applies the changes to the project state whose root is root, all or none: each one
    must find its node, and find there what it says was there before (a property's value,
    the child it removes, valid indices); otherwise the changes already applied are
    undone and the result says which change failed and why. A child is compared without
    its personal settings (SharedProject.h). Nothing goes into an undo history.
*/
juce::Result applyChanges(juce::ValueTree root, const ChangeSet& changes);

/**
    Values compared exactly: the same type and the same value (doubles bit for bit,
    strings and binary data byte for byte). juce::var's own comparison is looser (1 equals
    1.0), which would let two copies of a project drift apart unnoticed.
*/
[[nodiscard]] bool isSameValue(const juce::var& a, const juce::var& b);

/** The same type, the same properties with the same values (isSameValue(), in any order)
    and the same children in the same order, personal settings left out. */
[[nodiscard]] bool isSameTree(const juce::ValueTree& a, const juce::ValueTree& b);

/**
    What a change touches, for the locks of co-op sessions: a track (its clips, notes,
    mixer settings, effects, instrument, sends and automation), a bus, the master, or the
    project (tempo, metre, the loop, and which tracks and buses there are, in which order).
*/
struct EditScope
{
    enum class Kind : std::uint8_t
    {
        project,
        master,
        track,
        bus
    };

    Kind kind = Kind::project;
    ObjectId id = 0; ///< of the track or bus

    [[nodiscard]] static EditScope project() noexcept { return {}; }
    [[nodiscard]] static EditScope master() noexcept { return {Kind::master, 0}; }
    [[nodiscard]] static EditScope track(ObjectId trackId) noexcept { return {Kind::track, trackId}; }
    [[nodiscard]] static EditScope bus(ObjectId busId) noexcept { return {Kind::bus, busId}; }

    auto operator<=>(const EditScope&) const = default;
};

/** The scopes a change needs: the innermost track, bus or master on its path, else the
    project. Adding or removing a track or a bus needs the project and that track or bus
    too, so that nobody removes what someone else is working on. */
[[nodiscard]] std::vector<EditScope> scopesOf(const ProjectChange& change);

/** Every scope the changes need, sorted, each once. */
[[nodiscard]] std::vector<EditScope> scopesOf(const ChangeSet& changes);

} // namespace daw::model
