#include "daw/model/SharedProject.h"

#include "daw/model/Grid.h"
#include "daw/model/Project.h"
#include "daw/model/Track.h"

#include "Containers.h"
#include "FilePaths.h"
#include "Identifiers.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace daw::model
{

namespace
{

using PathConversion = std::function<juce::String(const juce::String&)>;

juce::String relativePath(const juce::String& path, const juce::File& folder)
{
    return path.isEmpty() ? juce::String() : detail::pathForFile(juce::File(path), folder);
}

juce::String absolutePath(const juce::String& path, const juce::File& folder)
{
    return path.isEmpty() ? juce::String() : detail::fileForPath(path, folder).getFullPathName();
}

juce::var convertedPath(const juce::var& value, const PathConversion& convert)
{
    return value.isString() ? juce::var(convert(value.toString())) : value;
}

// Personal settings out, paths converted, in tree and below it.
void makeShared(juce::ValueTree& tree, const PathConversion& convert)
{
    for (int index = tree.getNumProperties(); --index >= 0;)
    {
        const auto name = tree.getPropertyName(index);
        if (isPersonalProperty(tree.getType(), name))
            tree.removeProperty(name, nullptr);
        else if (isPathProperty(tree.getType(), name))
            tree.setProperty(name, convertedPath(tree[name], convert), nullptr);
    }

    for (auto child : tree)
        makeShared(child, convert);
}

void convertPaths(juce::ValueTree& tree, const PathConversion& convert)
{
    for (int index = 0; index < tree.getNumProperties(); ++index)
        if (const auto name = tree.getPropertyName(index); isPathProperty(tree.getType(), name))
            tree.setProperty(name, convertedPath(tree[name], convert), nullptr);

    for (auto child : tree)
        convertPaths(child, convert);
}

juce::Identifier nodeTypeAt(const NodePath& path)
{
    return path.empty() ? ids::project : path.back().type;
}

ProjectChange convertedChange(ProjectChange change, const PathConversion& convert,
                              const std::function<juce::ValueTree(const juce::ValueTree&)>& copy)
{
    switch (change.kind)
    {
    case ProjectChange::Kind::setProperty:
        if (isPathProperty(nodeTypeAt(change.path), change.property))
        {
            if (change.before)
                change.before = convertedPath(*change.before, convert);
            if (change.after)
                change.after = convertedPath(*change.after, convert);
        }
        break;
    case ProjectChange::Kind::insertChild:
    case ProjectChange::Kind::removeChild:
        change.child = copy(change.child);
        break;
    case ProjectChange::Kind::moveChild:
        break;
    }
    return change;
}

// What each node type may hold. Items come first, containers after them, once each.
struct Schema
{
    juce::Identifier item;                        // the repeated child type, if any
    std::vector<juce::Identifier> containers;     // children there is at most one of
    int maximumItems = std::numeric_limits<int>::max();
};

const Schema* schemaOf(const juce::Identifier& type)
{
    static const std::map<juce::String, Schema> schemas{
        {ids::project.toString(), {ids::track, {ids::buses, ids::master}}},
        {ids::track.toString(),
         {ids::clip, {ids::instrument, ids::effects, ids::sends, ids::automation}}},
        {ids::buses.toString(), {ids::bus, {}}},
        {ids::bus.toString(), {{}, {ids::effects, ids::automation}}},
        {ids::master.toString(), {{}, {ids::effects, ids::automation}}},
        {ids::effects.toString(), {ids::effect, {}}},
        {ids::instrument.toString(), {ids::effect, {}, 1}},
        {ids::sends.toString(), {ids::send, {}}},
        {ids::automation.toString(), {ids::lane, {}}},
        {ids::lane.toString(), {ids::point, {}}},
        {ids::effect.toString(), {{}, {ids::parameters, ids::plugin, ids::files}}},
        {ids::clip.toString(), {}},
        {ids::point.toString(), {}},
        {ids::send.toString(), {}},
        {ids::parameters.toString(), {}},
        {ids::plugin.toString(), {}},
        {ids::files.toString(), {}},
    };

    const auto found = schemas.find(type.toString());
    return found == schemas.end() ? nullptr : &found->second;
}

bool isContainerOf(const Schema& schema, const juce::Identifier& type)
{
    return std::find(schema.containers.begin(), schema.containers.end(), type)
           != schema.containers.end();
}

// Containers a project, track, bus or master always has, which no change may take away
// (a MIDI track's instrument container comes with its first instrument, and goes with undo).
bool isPermanentContainer(const juce::Identifier& parentType, const juce::Identifier& type)
{
    if (parentType == ids::project)
        return type == ids::buses || type == ids::master;
    if (parentType == ids::track || parentType == ids::bus || parentType == ids::master)
        return type == ids::effects || type == ids::sends || type == ids::automation;
    return false;
}

bool needsId(const juce::Identifier& type)
{
    return type == ids::track || type == ids::clip || type == ids::bus || type == ids::effect
           || type == ids::send;
}

juce::Result checkNode(const juce::ValueTree& node)
{
    const auto* schema = schemaOf(node.getType());
    if (schema == nullptr)
        return juce::Result::fail("unknown node type " + node.getType().toString());

    if (needsId(node.getType()) && !(node[ids::id].isInt() || node[ids::id].isInt64()))
        return juce::Result::fail(node.getType().toString() + " without an id");
    if (node.hasType(ids::lane) && !node[ids::target].isString())
        return juce::Result::fail("automation lane without a target");

    int items = 0;
    bool afterItems = false;
    std::set<juce::String> containersSeen;
    for (const auto& child : node)
    {
        if (schema->item.isValid() && child.hasType(schema->item))
        {
            if (afterItems)
                return juce::Result::fail(child.getType().toString() + " after the containers of "
                                          + node.getType().toString());
            if (++items > schema->maximumItems)
                return juce::Result::fail("too many children in " + node.getType().toString());
        }
        else if (isContainerOf(*schema, child.getType()))
        {
            afterItems = true;
            if (!containersSeen.insert(child.getType().toString()).second)
                return juce::Result::fail("two " + child.getType().toString() + " in "
                                          + node.getType().toString());
        }
        else
        {
            return juce::Result::fail(child.getType().toString() + " cannot be in "
                                      + node.getType().toString());
        }

        if (const auto result = checkNode(child); result.failed())
            return result;
    }

    return juce::Result::ok();
}

void collectIds(const juce::ValueTree& tree, std::vector<ObjectId>& found)
{
    if (tree.hasProperty(ids::id))
        found.push_back(static_cast<ObjectId>(tree[ids::id]));
    for (const auto& child : tree)
        collectIds(child, found);
}

int leadingItems(const juce::ValueTree& parent, const Schema& schema)
{
    return schema.item.isValid() ? detail::countLeading(parent, schema.item) : 0;
}

juce::Result checkInsertion(const juce::ValueTree& root, const juce::ValueTree& parent,
                            int index, const juce::ValueTree& child)
{
    const auto* schema = schemaOf(parent.getType());
    if (schema == nullptr || !child.isValid())
        return juce::Result::fail("nothing can go there");
    if (index < 0 || index > parent.getNumChildren())
        return juce::Result::fail("no such place");

    const bool isItem = schema->item.isValid() && child.hasType(schema->item);
    const int items = leadingItems(parent, *schema);

    if (isItem)
    {
        if (index > items)
            return juce::Result::fail(child.getType().toString() + " after the containers");
        if (items >= schema->maximumItems)
            return juce::Result::fail("too many children in " + parent.getType().toString());
    }
    else if (isContainerOf(*schema, child.getType()))
    {
        if (index < items)
            return juce::Result::fail(child.getType().toString() + " before the items");
        if (parent.getChildWithName(child.getType()).isValid())
            return juce::Result::fail("a second " + child.getType().toString());
    }
    else
    {
        return juce::Result::fail(child.getType().toString() + " cannot be in "
                                  + parent.getType().toString());
    }

    if (const auto result = checkNode(child); result.failed())
        return result;

    std::vector<ObjectId> newIds;
    collectIds(child, newIds);
    std::sort(newIds.begin(), newIds.end());
    if (std::adjacent_find(newIds.begin(), newIds.end()) != newIds.end())
        return juce::Result::fail("an id twice");
    for (const auto id : newIds)
        if (containsId(root, id))
            return juce::Result::fail("id " + juce::String(id) + " is taken");

    return juce::Result::ok();
}

} // namespace

bool isPersonalProperty(const juce::Identifier& nodeType, const juce::Identifier& property)
{
    if (nodeType == ids::project)
        return property == ids::snapEnabled || property == ids::gridDivision
               || property == ids::metronomeEnabled || property == ids::metronomeVolume
               || property == ids::countInEnabled || property == ids::countInBars
               || property == ids::punchEnabled || property == ids::recordingBitsPerSample
               || property == ids::idRangeFirst || property == ids::idRangeLast;

    if (nodeType == ids::track)
        return property == ids::armed || property == ids::inputChannel
               || property == ids::inputStereo || property == ids::monitoring
               || property == ids::midiInputDevice || property == ids::midiInputChannel
               || property == ids::solo;

    return false;
}

bool isPathProperty(const juce::Identifier& nodeType, const juce::Identifier& property)
{
    return (nodeType == ids::clip && property == ids::audioFile) || nodeType == ids::files;
}

juce::ValueTree sharedCopy(const juce::ValueTree& tree, const juce::File& folder)
{
    auto copy = tree.createCopy();
    makeShared(copy, [&folder](const juce::String& path) { return relativePath(path, folder); });
    return copy;
}

juce::ValueTree localCopy(const juce::ValueTree& sharedTree, const juce::File& folder)
{
    auto copy = sharedTree.createCopy();
    convertPaths(copy, [&folder](const juce::String& path) { return absolutePath(path, folder); });
    addDefaultPersonalSettings(copy);
    return copy;
}

std::optional<ProjectChange> sharedChange(const ProjectChange& change, const juce::File& folder)
{
    if (change.kind == ProjectChange::Kind::setProperty
        && isPersonalProperty(nodeTypeAt(change.path), change.property))
        return std::nullopt;

    return convertedChange(
        change, [&folder](const juce::String& path) { return relativePath(path, folder); },
        [&folder](const juce::ValueTree& tree) { return sharedCopy(tree, folder); });
}

ProjectChange localChange(const ProjectChange& sharedChangeToConvert, const juce::File& folder)
{
    return convertedChange(
        sharedChangeToConvert,
        [&folder](const juce::String& path) { return absolutePath(path, folder); },
        [&folder](const juce::ValueTree& tree) { return localCopy(tree, folder); });
}

ChangeSet sharedChanges(const ChangeSet& changes, const juce::File& folder)
{
    ChangeSet shared;
    shared.reserve(changes.size());
    for (const auto& change : changes)
        if (auto converted = sharedChange(change, folder))
            shared.push_back(std::move(*converted));
    return shared;
}

ChangeSet localChanges(const ChangeSet& changes, const juce::File& folder)
{
    ChangeSet local;
    local.reserve(changes.size());
    for (const auto& change : changes)
        local.push_back(localChange(change, folder));
    return local;
}

void removePersonalSettings(juce::ValueTree& tree)
{
    for (int index = tree.getNumProperties(); --index >= 0;)
        if (const auto name = tree.getPropertyName(index); isPersonalProperty(tree.getType(), name))
            tree.removeProperty(name, nullptr);

    for (auto child : tree)
        removePersonalSettings(child);
}

void addDefaultPersonalSettings(juce::ValueTree& tree)
{
    if (tree.hasType(ids::project))
    {
        const auto setDefault = [&tree](const juce::Identifier& name, const juce::var& value)
        {
            if (!tree.hasProperty(name))
                tree.setProperty(name, value, nullptr);
        };
        setDefault(ids::snapEnabled, true);
        setDefault(ids::gridDivision, toString(GridDivision::automatic));
        setDefault(ids::metronomeEnabled, false);
        setDefault(ids::metronomeVolume, Project::defaultMetronomeVolume);
        setDefault(ids::countInEnabled, false);
        setDefault(ids::countInBars, 1);
        setDefault(ids::punchEnabled, false);
        setDefault(ids::recordingBitsPerSample, Project::defaultRecordingBitsPerSample);
    }
    else if (tree.hasType(ids::track))
    {
        Track track(tree);
        if (!tree.hasProperty(ids::armed))
            track.setArmed(false);
        if (!tree.hasProperty(ids::solo))
            track.setSoloed(false);
        if (!tree.hasProperty(ids::inputChannel) || !tree.hasProperty(ids::inputStereo))
            track.setInput(track.isMidi() ? TrackInput::none() : TrackInput{});
        if (!tree.hasProperty(ids::monitoring))
            track.setMonitoring(MonitoringMode::automatic);
        if (track.isMidi()
            && (!tree.hasProperty(ids::midiInputDevice) || !tree.hasProperty(ids::midiInputChannel)))
            track.setMidiInput({});
    }

    for (auto child : tree)
        addDefaultPersonalSettings(child);
}

IdRange idRangeOfParticipant(int participantNumber) noexcept
{
    jassert(participantNumber >= 1);
    const auto number = static_cast<ObjectId>(std::max(1, participantNumber));
    return {number << 40, ((number + 1) << 40) - 1};
}

void setIdRange(juce::ValueTree projectState, std::optional<IdRange> range)
{
    if (range.has_value())
    {
        projectState.setProperty(ids::idRangeFirst, range->first, nullptr);
        projectState.setProperty(ids::idRangeLast, range->last, nullptr);
    }
    else
    {
        projectState.removeProperty(ids::idRangeFirst, nullptr);
        projectState.removeProperty(ids::idRangeLast, nullptr);
    }
}

std::optional<IdRange> getIdRange(const juce::ValueTree& projectState)
{
    if (!projectState.hasProperty(ids::idRangeFirst) || !projectState.hasProperty(ids::idRangeLast))
        return std::nullopt;

    return IdRange{static_cast<ObjectId>(projectState[ids::idRangeFirst]),
                   static_cast<ObjectId>(projectState[ids::idRangeLast])};
}

bool containsId(const juce::ValueTree& tree, ObjectId id)
{
    if (tree.hasProperty(ids::id) && static_cast<ObjectId>(tree[ids::id]) == id)
        return true;

    return std::any_of(tree.begin(), tree.end(),
                       [id](const juce::ValueTree& child) { return containsId(child, id); });
}

std::vector<ObjectId> idsIn(const juce::ValueTree& tree)
{
    std::vector<ObjectId> found;
    collectIds(tree, found);
    return found;
}

juce::Result checkChange(const juce::ValueTree& projectRoot, const ProjectChange& change)
{
    const auto node = findNode(projectRoot, change.path);
    if (!node.isValid())
        return juce::Result::fail("no such node");

    switch (change.kind)
    {
    case ProjectChange::Kind::setProperty:
        if (isPersonalProperty(node.getType(), change.property))
            return juce::Result::fail("a personal setting");
        if ((change.property == ids::id && needsId(node.getType()))
            || (change.property == ids::target && node.hasType(ids::lane)))
            return juce::Result::fail("what identifies a node cannot change");
        return juce::Result::ok();

    case ProjectChange::Kind::insertChild:
        return checkInsertion(projectRoot, node, change.index, change.child);

    case ProjectChange::Kind::removeChild:
        if (change.index < 0 || change.index >= node.getNumChildren())
            return juce::Result::fail("no such child");
        if (isPermanentContainer(node.getType(), node.getChild(change.index).getType()))
            return juce::Result::fail("a container cannot go");
        return juce::Result::ok();

    case ProjectChange::Kind::moveChild:
    {
        const auto* schema = schemaOf(node.getType());
        if (schema == nullptr || change.index < 0 || change.newIndex < 0)
            return juce::Result::fail("no such child");
        const auto moved = node.getChild(change.index);
        const bool isItem = schema->item.isValid() && moved.hasType(schema->item);
        if (!isItem || change.newIndex >= leadingItems(node, *schema))
            return juce::Result::fail("only items move among items");
        return juce::Result::ok();
    }
    }

    return juce::Result::fail("unknown change");
}

juce::Result checkProject(const juce::ValueTree& projectRoot)
{
    if (!projectRoot.hasType(ids::project))
        return juce::Result::fail("not a project");
    if (!projectRoot.getChildWithName(ids::buses).isValid()
        || !projectRoot.getChildWithName(ids::master).isValid())
        return juce::Result::fail("a project without its buses or master");

    if (const auto result = checkNode(projectRoot); result.failed())
        return result;

    auto ids = idsIn(projectRoot);
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
        return juce::Result::fail("an id twice");
    return juce::Result::ok();
}

} // namespace daw::model
