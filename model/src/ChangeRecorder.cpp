#include "daw/model/ChangeRecorder.h"

#include "daw/model/SharedProject.h"

#include <utility>

namespace daw::model
{

namespace
{

// Indices from the root down to node. The shadow has the same structure as the state
// up to the change being reported, so the same indices lead to the same node there.
std::vector<int> indicesOf(const juce::ValueTree& node)
{
    std::vector<int> indices;
    for (auto current = node; current.getParent().isValid(); current = current.getParent())
        indices.push_back(current.getParent().indexOf(current));
    return {indices.rbegin(), indices.rend()};
}

juce::ValueTree follow(juce::ValueTree root, const std::vector<int>& indices)
{
    for (const auto index : indices)
    {
        root = root.getChild(index);
        if (!root.isValid())
            break;
    }
    return root;
}

} // namespace

ChangeRecorder::ChangeRecorder(juce::ValueTree projectState)
    : state(std::move(projectState)), shadow(state.createCopy())
{
    state.addListener(this);
}

ChangeRecorder::~ChangeRecorder()
{
    state.removeListener(this);
}

ChangeSet ChangeRecorder::takeChanges()
{
    return std::exchange(changes, {});
}

void ChangeRecorder::record(ProjectChange change)
{
    if (recording)
        changes.push_back(std::move(change));
}

void ChangeRecorder::valueTreePropertyChanged(juce::ValueTree& tree,
                                              const juce::Identifier& property)
{
    auto node = follow(shadow, indicesOf(tree));
    jassert(node.isValid() && node.getType() == tree.getType());
    if (!node.isValid())
        return;

    ProjectChange change;
    change.kind = ProjectChange::Kind::setProperty;
    change.path = pathOf(node); // before the change, as everyone else has it
    change.property = property;
    if (node.hasProperty(property))
        change.before = node[property];
    if (tree.hasProperty(property))
        change.after = tree[property];

    if (change.after.has_value())
        node.setProperty(property, *change.after, nullptr);
    else
        node.removeProperty(property, nullptr);

    if (!isPersonalProperty(tree.getType(), property))
        record(std::move(change));
}

void ChangeRecorder::valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child)
{
    auto node = follow(shadow, indicesOf(parent));
    jassert(node.isValid());
    if (!node.isValid())
        return;

    ProjectChange change;
    change.kind = ProjectChange::Kind::insertChild;
    change.path = pathOf(node);
    change.index = parent.indexOf(child);
    change.child = child.createCopy();

    node.addChild(child.createCopy(), change.index, nullptr);
    record(std::move(change));
}

void ChangeRecorder::valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree&, int index)
{
    auto node = follow(shadow, indicesOf(parent));
    jassert(node.isValid() && index < node.getNumChildren());
    if (!node.isValid() || index >= node.getNumChildren())
        return;

    ProjectChange change;
    change.kind = ProjectChange::Kind::removeChild;
    change.path = pathOf(node);
    change.index = index;
    change.child = node.getChild(index).createCopy();

    node.removeChild(index, nullptr);
    record(std::move(change));
}

void ChangeRecorder::valueTreeChildOrderChanged(juce::ValueTree& parent, int oldIndex, int newIndex)
{
    auto node = follow(shadow, indicesOf(parent));
    jassert(node.isValid());
    if (!node.isValid())
        return;

    ProjectChange change;
    change.kind = ProjectChange::Kind::moveChild;
    change.path = pathOf(node);
    change.index = oldIndex;
    change.newIndex = newIndex;

    node.moveChild(oldIndex, newIndex, nullptr);
    record(std::move(change));
}

void ChangeRecorder::valueTreeRedirected(juce::ValueTree&)
{
    // The state now refers to other data: nothing before it can be described as changes.
    jassertfalse;
    shadow = state.createCopy();
}

} // namespace daw::model
