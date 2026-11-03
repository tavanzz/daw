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

/** The engine's instances for an owner's effects: one slot per effect, in order,
    with its instance (null for an unknown type) and revival count. Without one,
    every effect gets a fresh instance (exports). */
using EffectLookup = std::function<std::vector<engine::ChainSlot>(
    const ChainOwner& owner, const model::EffectList& effects)>;

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

    Runs on the thread that owns the project (the message thread, or an export's
    own copy). Allocates.
*/
[[nodiscard]] std::unique_ptr<engine::Arrangement>
buildArrangement(const model::Project& project, double sampleRate, const SourceLookup& sources,
                 const MeterLookup& meters = {}, const EffectLookup& effects = {});

/** An automation lane as the engine plays it, at sampleRate; null without points. */
[[nodiscard]] std::shared_ptr<const engine::AutomationCurve>
makeCurve(const model::AutomationLane& lane, double sampleRate);

} // namespace daw::controller
