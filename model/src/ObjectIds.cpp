#include "ObjectIds.h"

#include "daw/model/SharedProject.h"

#include "Identifiers.h"

#include <algorithm>

namespace daw::model::detail
{

namespace
{

ObjectId largestId(const juce::ValueTree& tree)
{
    auto largest = static_cast<ObjectId>(tree[ids::id]);

    for (const auto& child : tree)
        largest = std::max(largest, largestId(child));

    return largest;
}

ObjectId largestIdIn(const juce::ValueTree& tree, const IdRange& range, ObjectId largest)
{
    if (tree.hasProperty(ids::id))
        if (const auto id = static_cast<ObjectId>(tree[ids::id]); range.contains(id))
            largest = std::max(largest, id);

    for (const auto& child : tree)
        largest = largestIdIn(child, range, largest);

    return largest;
}

} // namespace

ObjectId nextObjectId(const juce::ValueTree& node)
{
    const auto root = node.getRoot();

    if (const auto range = getIdRange(root))
    {
        const auto largest = largestIdIn(root, *range, range->first - 1);
        jassert(largest < range->last);
        return largest + 1;
    }

    return largestId(root) + 1;
}

} // namespace daw::model::detail
