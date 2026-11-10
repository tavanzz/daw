#include "daw/model/ProjectChanges.h"

#include "daw/model/SharedProject.h"

#include "Identifiers.h"

#include <algorithm>
#include <bit>

namespace daw::model
{

namespace
{

bool isKeyedById(const juce::Identifier& type)
{
    return type == ids::track || type == ids::clip || type == ids::bus || type == ids::effect
           || type == ids::send;
}

PathStep stepTo(const juce::ValueTree& child)
{
    PathStep step;
    step.type = child.getType();

    if (isKeyedById(step.type))
    {
        step.key = PathStep::Key::id;
        step.id = static_cast<ObjectId>(child[ids::id]);
        return step;
    }

    if (step.type == ids::lane)
    {
        step.key = PathStep::Key::target;
        step.target = child[ids::target].toString();
        return step;
    }

    step.key = PathStep::Key::index;
    const auto parent = child.getParent();
    for (const auto& sibling : parent)
    {
        if (sibling == child)
            break;
        if (sibling.hasType(step.type))
            ++step.index;
    }
    return step;
}

bool matchesStep(const juce::ValueTree& child, const PathStep& step)
{
    switch (step.key)
    {
    case PathStep::Key::id:
        return child.hasProperty(ids::id) && static_cast<ObjectId>(child[ids::id]) == step.id;
    case PathStep::Key::target:
        return child[ids::target].toString() == step.target;
    case PathStep::Key::index:
        return false; // counted by the caller
    }
    return false;
}

juce::String describe(const NodePath& path)
{
    juce::String text = "project";
    for (const auto& step : path)
    {
        text << " / " << step.type.toString();
        if (step.key == PathStep::Key::id)
            text << ' ' << juce::String(step.id);
        else if (step.key == PathStep::Key::index)
            text << " #" << juce::String(step.index);
    }
    return text;
}

bool hasValue(const juce::ValueTree& node, const juce::Identifier& property,
              const std::optional<juce::var>& value)
{
    if (!value.has_value())
        return !node.hasProperty(property);

    return node.hasProperty(property) && isSameValue(node[property], *value);
}

juce::Result applyOne(juce::ValueTree& root, const ProjectChange& change)
{
    auto node = findNode(root, change.path);
    if (!node.isValid())
        return juce::Result::fail("there is no " + describe(change.path));

    switch (change.kind)
    {
    case ProjectChange::Kind::setProperty:
        if (!hasValue(node, change.property, change.before))
            return juce::Result::fail(change.property.toString() + " of " + describe(change.path)
                                      + " is not what the change expects");
        if (change.after.has_value())
            node.setProperty(change.property, *change.after, nullptr);
        else
            node.removeProperty(change.property, nullptr);
        return juce::Result::ok();

    case ProjectChange::Kind::insertChild:
        if (!change.child.isValid() || change.index < 0 || change.index > node.getNumChildren())
            return juce::Result::fail("no place " + juce::String(change.index) + " in "
                                      + describe(change.path));
        node.addChild(change.child.createCopy(), change.index, nullptr);
        return juce::Result::ok();

    case ProjectChange::Kind::removeChild:
        if (change.index < 0 || change.index >= node.getNumChildren())
            return juce::Result::fail("no child " + juce::String(change.index) + " in "
                                      + describe(change.path));
        if (!isSameTree(node.getChild(change.index), change.child))
            return juce::Result::fail("child " + juce::String(change.index) + " of "
                                      + describe(change.path)
                                      + " is not what the change expects");
        node.removeChild(change.index, nullptr);
        return juce::Result::ok();

    case ProjectChange::Kind::moveChild:
        if (change.index < 0 || change.index >= node.getNumChildren() || change.newIndex < 0
            || change.newIndex >= node.getNumChildren())
            return juce::Result::fail("cannot move child " + juce::String(change.index) + " to "
                                      + juce::String(change.newIndex) + " in "
                                      + describe(change.path));
        node.moveChild(change.index, change.newIndex, nullptr);
        return juce::Result::ok();
    }

    return juce::Result::fail("unknown change");
}

int countSharedProperties(const juce::ValueTree& tree)
{
    int count = 0;
    for (int index = 0; index < tree.getNumProperties(); ++index)
        if (!isPersonalProperty(tree.getType(), tree.getPropertyName(index)))
            ++count;
    return count;
}

} // namespace

NodePath pathOf(const juce::ValueTree& node)
{
    NodePath path;
    for (auto current = node; current.getParent().isValid(); current = current.getParent())
        path.push_back(stepTo(current));

    std::reverse(path.begin(), path.end());
    return path;
}

juce::ValueTree findNode(const juce::ValueTree& root, const NodePath& path)
{
    auto current = root;

    for (const auto& step : path)
    {
        juce::ValueTree found;
        int sameType = 0;

        for (const auto& child : current)
        {
            if (!child.hasType(step.type))
                continue;

            if (step.key == PathStep::Key::index ? sameType == step.index
                                                 : matchesStep(child, step))
            {
                found = child;
                break;
            }
            ++sameType;
        }

        if (!found.isValid())
            return {};
        current = found;
    }

    return current;
}

ProjectChange inverted(const ProjectChange& change)
{
    auto inverse = change;

    switch (change.kind)
    {
    case ProjectChange::Kind::setProperty:
        std::swap(inverse.before, inverse.after);
        break;
    case ProjectChange::Kind::insertChild:
        inverse.kind = ProjectChange::Kind::removeChild;
        break;
    case ProjectChange::Kind::removeChild:
        inverse.kind = ProjectChange::Kind::insertChild;
        break;
    case ProjectChange::Kind::moveChild:
        std::swap(inverse.index, inverse.newIndex);
        break;
    }

    return inverse;
}

ChangeSet inverted(const ChangeSet& changes)
{
    ChangeSet inverse;
    inverse.reserve(changes.size());
    for (auto change = changes.rbegin(); change != changes.rend(); ++change)
        inverse.push_back(inverted(*change));
    return inverse;
}

juce::Result applyChanges(juce::ValueTree root, const ChangeSet& changes)
{
    for (std::size_t index = 0; index < changes.size(); ++index)
    {
        const auto result = applyOne(root, changes[index]);
        if (result.wasOk())
            continue;

        for (auto applied = index; applied-- > 0;)
        {
            [[maybe_unused]] const auto undone = applyOne(root, inverted(changes[applied]));
            jassert(undone.wasOk());
        }

        return juce::Result::fail("change " + juce::String(static_cast<int>(index) + 1) + " of "
                                  + juce::String(static_cast<int>(changes.size())) + ": "
                                  + result.getErrorMessage());
    }

    return juce::Result::ok();
}

bool isSameValue(const juce::var& a, const juce::var& b)
{
    if (a.isVoid() || b.isVoid())
        return a.isVoid() && b.isVoid();
    if (a.isUndefined() || b.isUndefined())
        return a.isUndefined() && b.isUndefined();
    if (a.isBool())
        return b.isBool() && static_cast<bool>(a) == static_cast<bool>(b);
    if (a.isInt())
        return b.isInt() && static_cast<int>(a) == static_cast<int>(b);
    if (a.isInt64())
        return b.isInt64() && static_cast<juce::int64>(a) == static_cast<juce::int64>(b);
    if (a.isDouble())
        return b.isDouble()
               && std::bit_cast<std::uint64_t>(static_cast<double>(a))
                      == std::bit_cast<std::uint64_t>(static_cast<double>(b));
    if (a.isString())
        return b.isString() && a.toString() == b.toString();
    if (a.isBinaryData())
        return b.isBinaryData() && *a.getBinaryData() == *b.getBinaryData();
    if (a.isArray())
    {
        if (!b.isArray() || a.size() != b.size())
            return false;
        for (int index = 0; index < a.size(); ++index)
            if (!isSameValue(a[index], b[index]))
                return false;
        return true;
    }

    return a.getDynamicObject() != nullptr && a.getDynamicObject() == b.getDynamicObject();
}

bool isSameTree(const juce::ValueTree& a, const juce::ValueTree& b)
{
    if (!a.isValid() || !b.isValid())
        return a.isValid() == b.isValid();
    if (a.getType() != b.getType() || a.getNumChildren() != b.getNumChildren())
        return false;

    for (int index = 0; index < a.getNumProperties(); ++index)
    {
        const auto name = a.getPropertyName(index);
        if (isPersonalProperty(a.getType(), name))
            continue;
        if (!b.hasProperty(name) || !isSameValue(a[name], b[name]))
            return false;
    }
    if (countSharedProperties(a) != countSharedProperties(b))
        return false;

    for (int index = 0; index < a.getNumChildren(); ++index)
        if (!isSameTree(a.getChild(index), b.getChild(index)))
            return false;

    return true;
}

std::vector<EditScope> scopesOf(const ProjectChange& change)
{
    auto innermost = EditScope::project();
    for (const auto& step : change.path)
    {
        if (step.type == ids::track)
            innermost = EditScope::track(step.id);
        else if (step.type == ids::bus)
            innermost = EditScope::bus(step.id);
        else if (step.type == ids::master)
            innermost = EditScope::master();
    }

    // The master's volume is a property of the project's root.
    if (change.path.empty() && change.kind == ProjectChange::Kind::setProperty
        && change.property == ids::masterVolume)
        innermost = EditScope::master();

    std::vector<EditScope> scopes{innermost};

    const bool addsOrRemoves = change.kind == ProjectChange::Kind::insertChild
                               || change.kind == ProjectChange::Kind::removeChild;
    if (addsOrRemoves && innermost.kind == EditScope::Kind::project)
    {
        const auto id = static_cast<ObjectId>(change.child[ids::id]);
        if (change.child.hasType(ids::track))
            scopes.push_back(EditScope::track(id));
        else if (change.child.hasType(ids::bus))
            scopes.push_back(EditScope::bus(id));
    }

    return scopes;
}

std::vector<EditScope> scopesOf(const ChangeSet& changes)
{
    std::vector<EditScope> scopes;
    for (const auto& change : changes)
        for (const auto& scope : scopesOf(change))
            scopes.push_back(scope);

    std::sort(scopes.begin(), scopes.end());
    scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());
    return scopes;
}

} // namespace daw::model
