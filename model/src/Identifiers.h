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

// Containers: after the tracks of a project, after the clips of a track.
inline const juce::Identifier buses{"Buses"};
inline const juce::Identifier master{"Master"};
inline const juce::Identifier effects{"Effects"};
inline const juce::Identifier sends{"Sends"};
inline const juce::Identifier automation{"Automation"};

inline const juce::Identifier bus{"Bus"};
inline const juce::Identifier effect{"Effect"};
inline const juce::Identifier parameters{"Parameters"};
inline const juce::Identifier send{"Send"};
inline const juce::Identifier lane{"Lane"};
inline const juce::Identifier point{"Point"};
inline const juce::Identifier instrument{"Instrument"};
inline const juce::Identifier files{"Files"};
inline const juce::Identifier plugin{"Plugin"};

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
inline const juce::Identifier metronomeEnabled{"metronomeEnabled"};
inline const juce::Identifier metronomeVolume{"metronomeVolume"};
inline const juce::Identifier countInEnabled{"countInEnabled"};
inline const juce::Identifier countInBars{"countInBars"};
inline const juce::Identifier punchEnabled{"punchEnabled"};
inline const juce::Identifier recordingBitsPerSample{"recordingBitsPerSample"};

// Tracks and clips
inline const juce::Identifier id{"id"};
inline const juce::Identifier pluginFormat{"format"};
inline const juce::Identifier pluginUniqueId{"uniqueId"};
inline const juce::Identifier pluginManufacturer{"manufacturer"};
inline const juce::Identifier pluginVersion{"version"};
inline const juce::Identifier pluginCategory{"category"};
inline const juce::Identifier pluginIdentifier{"identifier"};
inline const juce::Identifier pluginInstrument{"instrument"};
inline const juce::Identifier name{"name"};
inline const juce::Identifier volume{"volume"};
inline const juce::Identifier pan{"pan"};
inline const juce::Identifier mute{"mute"};
inline const juce::Identifier solo{"solo"};
inline const juce::Identifier inputChannel{"inputChannel"};
inline const juce::Identifier inputStereo{"inputStereo"};
inline const juce::Identifier monitoring{"monitoring"};
inline const juce::Identifier armed{"armed"};
inline const juce::Identifier audioFile{"audioFile"};
inline const juce::Identifier position{"position"};
inline const juce::Identifier offset{"offset"};
inline const juce::Identifier length{"length"};

// MIDI (Phase 6)
inline const juce::Identifier kind{"kind"};
inline const juce::Identifier midiInputDevice{"midiInputDevice"};
inline const juce::Identifier midiInputChannel{"midiInputChannel"};
inline const juce::Identifier midiChannel{"midiChannel"};
inline const juce::Identifier notes{"notes"};
inline const juce::Identifier controllers{"controllers"};

// Effects, sends, buses and automation
inline const juce::Identifier type{"type"};
inline const juce::Identifier bypass{"bypass"};
inline const juce::Identifier data{"data"};
inline const juce::Identifier busId{"bus"};
inline const juce::Identifier level{"level"};
inline const juce::Identifier preFader{"preFader"};
inline const juce::Identifier mode{"mode"};
inline const juce::Identifier target{"target"};
inline const juce::Identifier value{"value"};
inline const juce::Identifier curve{"curve"};

} // namespace daw::model::ids
