#pragma once

#include <juce_core/juce_core.h>

// ValueTree types and property names of the in-memory project state. They are not
// the on-disk format: ProjectFile maps them to JSON explicitly, so renaming one here
// never changes saved files.
namespace daw::model::ids
{

inline const juce::Identifier project{"Project"};
inline const juce::Identifier track{"Track"};
inline const juce::Identifier clip{"Clip"};

inline const juce::Identifier loopEnabled{"loopEnabled"};
inline const juce::Identifier name{"name"};
inline const juce::Identifier audioFile{"audioFile"};

} // namespace daw::model::ids
