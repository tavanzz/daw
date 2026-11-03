#pragma once

#include "Identifiers.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model::detail
{

// A project's children are its tracks, then the Buses and Master containers; a
// track's are its clips, then Effects, Sends and Automation; a bus has Effects and
// Automation. Containers go last so that tracks and clips keep their indices.

/** How many children of type the tree has before its first child of another type. */
[[nodiscard]] inline int countLeading(const juce::ValueTree& tree, const juce::Identifier& type)
{
    int count = tree.getNumChildren();
    while (count > 0 && !tree.getChild(count - 1).hasType(type))
        --count;
    return count;
}

inline void addContainer(juce::ValueTree& tree, const juce::Identifier& type)
{
    if (tree.getChildWithName(type).isValid())
        return;

    juce::ValueTree container(type);
    if (type == ids::automation)
        container.setProperty(ids::mode, "read", nullptr);
    tree.appendChild(container, nullptr);
}

inline void addMixContainers(juce::ValueTree& tree)
{
    addContainer(tree, ids::effects);
    if (tree.hasType(ids::track))
        addContainer(tree, ids::sends);
    addContainer(tree, ids::automation);
}

inline void addProjectContainers(juce::ValueTree& project)
{
    addContainer(project, ids::buses);
    addContainer(project, ids::master);
    auto master = project.getChildWithName(ids::master);
    addMixContainers(master);

    for (auto child : project)
        if (child.hasType(ids::track))
            addMixContainers(child);
    for (auto bus : project.getChildWithName(ids::buses))
        addMixContainers(bus);
}

} // namespace daw::model::detail
