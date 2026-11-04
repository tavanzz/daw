#pragma once

#include "daw/controller/ChainOwner.h"
#include "daw/engine/Arrangement.h"
#include "daw/model/Project.h"

#include <functional>
#include <memory>

namespace daw::controller
{

/** The loaded audio of a file at the rate being built for; null if it is not loaded. */
using SourceLookup =
    std::function<std::shared_ptr<const engine::AudioSource>(const juce::File& file)>;

/** The meter the UI reads for a track or a bus; null for none (e.g. exports). */
using MeterLookup =
    std::function<std::shared_ptr<engine::dsp::PeakLevelMeter>(model::ObjectId trackId)>;

/** The engine's instances for an owner's chain (a MIDI track's instrument first,
    then its effects): one slot per effect, in order, with its instance (null for an
    unknown type) and revival count. Without one, every effect gets a fresh instance
    (exports). */
using EffectLookup = std::function<std::vector<engine::ChainSlot>(
    const ChainOwner& owner, const std::vector<model::Effect>& effects)>;

/** The index the engine knows a MIDI input device by, from its name; -1 if no such
    device is connected. */
using MidiDeviceLookup = std::function<int(const juce::String& deviceName)>;

/** A sample an instrument plays, loaded at the rate being built for; null while it
    loads (or if it failed). */
using SampleLookup = std::function<std::shared_ptr<const engine::Sample>(const juce::File& file)>;

/**
    The engine's view of a project at one sample rate: ticks become samples, mute,
    solo, volume and pan become track gains, overlapping clips are resolved, the
    tempo map becomes the metronome's beats.

    - Effects become chain slots: their values normalised in the order of the
      effect's parameters (missing ones take their defaults), bypass switches them
      off. Effects of unknown types are left out.
    - Buses become aux buses (muted: gain 0), sends become sends to them: a track
      that is not heard (muted, or another soloed) sends nothing.
    - Automation in read mode becomes curves (ticks to samples), for what is heard;
      lanes without points are ignored.
    - A clip whose file is not loaded hides what it covers and plays silence; one of
      unknown length (not loaded yet) is left out.
    - A clip that reaches to within one sample of its file's end plays to the end of
      the file, so converting a whole file to another rate never cuts its last sample
      (and never gives it a fade).
    - MIDI tracks play their instrument first in their chain, and their MIDI clips as
      notes and controller lanes in samples: a clip plays where the clips above it do
      not cover it; a note plays if it starts there, up to where it ends or the
      clip's visible part does (at least one sample); controllers step within the
      visible parts, start at their value there and return to rest at the end. A
      track that is not heard plays no notes. Its MIDI input is found by device
      name (midiDevices; without one, every device). Instruments get the samples of
      their files (samples; without one, none).

    Runs on the thread that owns the project (the message thread, or an export's
    own copy). Allocates.
*/
[[nodiscard]] std::unique_ptr<engine::Arrangement>
buildArrangement(const model::Project& project, double sampleRate, const SourceLookup& sources,
                 const MeterLookup& meters = {}, const EffectLookup& effects = {},
                 const MidiDeviceLookup& midiDevices = {}, const SampleLookup& samples = {});

/** The identity a note has in every arrangement built from its clip. */
[[nodiscard]] std::uint64_t noteIdentity(model::ObjectId clipId, std::int32_t noteId) noexcept;

/** An automation lane as the engine plays it, at sampleRate; null without points. */
[[nodiscard]] std::shared_ptr<const engine::AutomationCurve>
makeCurve(const model::AutomationLane& lane, double sampleRate);

} // namespace daw::controller
