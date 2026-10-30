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

// Project
inline const juce::Identifier tempo{"tempo"};
inline const juce::Identifier timeSignatureNumerator{"timeSignatureNumerator"};
inline const juce::Identifier timeSignatureDenominator{"timeSignatureDenominator"};
inline const juce::Identifier loopEnabled{"loopEnabled"};
inline const juce::Identifier loopStart{"loopStart"};
inline const juce::Identifier loopEnd{"loopEnd"};
inline const juce::Identifier snapEnabled{"snapEnabled"};
inline const juce::Identifier gridDivision{"gridDivision"};
inline const juce::Identifier masterVolume{"masterVolume"};

// Tracks and clips
inline const juce::Identifier id{"id"};
inline const juce::Identifier name{"name"};
inline const juce::Identifier volume{"volume"};
inline const juce::Identifier pan{"pan"};
inline const juce::Identifier mute{"mute"};
inline const juce::Identifier solo{"solo"};
inline const juce::Identifier audioFile{"audioFile"};
inline const juce::Identifier position{"position"};
inline const juce::Identifier offset{"offset"};
inline const juce::Identifier length{"length"};

} // namespace daw::model::ids
