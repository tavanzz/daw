#include "ObjectIds.h"

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

} // namespace

ObjectId nextObjectId(const juce::ValueTree& node)
{
    return largestId(node.getRoot()) + 1;
}

} // namespace daw::model::detail
