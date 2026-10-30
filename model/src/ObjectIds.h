#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model::detail
{

/** One more than the largest id anywhere in the tree that node belongs to, so that
    tracks and clips share one id space.

    Ids are not stored as a counter: undoing an addition and adding again may reuse
    an id, but only after the object that had it is gone from the project and from
    the redo history (a new edit clears it), so two objects never share one. */
[[nodiscard]] ObjectId nextObjectId(const juce::ValueTree& node);

} // namespace daw::model::detail
