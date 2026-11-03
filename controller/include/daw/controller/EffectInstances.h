#pragma once

#include "daw/controller/ChainOwner.h"
#include "daw/engine/Arrangement.h"
#include "daw/model/Effect.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace daw::controller
{

/**
    The engine's instances of a project's effects, one per effect of the model, kept
    from one arrangement to the next so that their state (tails, envelopes) goes on.

    - An effect gets a new instance when it first appears, when its type changes,
      when the sample rate or block size changes, and when it moves within its chain
      (the old one then fades out where it was, the new one fades in where it went:
      the same instance cannot be in two places at once).
    - New instances fade in when the session runs (fadeIn), not when a project is
      opened or the rate changes.
    - Effects of unknown types get no instance (they are left out, silently).

    Message thread.
*/
class EffectInstances
{
public:
    /** Instances are made for this format; a different one drops them all. */
    void prepare(double sampleRate, int maximumBlockSize);

    /** Forgets every instance (another project). */
    void clear();

    /** The chain of an owner: one slot per effect, in order, with its instance (null
        for an unknown type) and revival count; parameters and automation are left
        to the caller. */
    [[nodiscard]] std::vector<engine::ChainSlot> chainFor(const ChainOwner& owner,
                                                          const model::EffectList& effects,
                                                          bool fadeIn);

    /** The instance an effect has now; null if none. */
    [[nodiscard]] std::shared_ptr<engine::fx::EffectInstance> find(model::ObjectId effectId) const;

    /** The user moved this effect: at the next build it is the one that starts anew
        (when two effects swap, the order alone cannot say which one moved). */
    void markMoved(model::ObjectId effectId) { moved.insert(effectId); }

    /** After a NaN or infinity: the effect plays again from a clean state. */
    void revive(model::ObjectId effectId) { ++revivals[effectId]; }

    /** Drops the instances of effects that no chain asked for since the last call
        (the arrangements still holding them keep them alive). */
    void forgetUnused();

    /** A fresh instance of a built-in effect for this format; null for an unknown type. */
    [[nodiscard]] static std::shared_ptr<engine::fx::EffectInstance>
    makeInstance(const juce::String& type, double sampleRate, int maximumBlockSize, bool fadeIn);

private:
    double sampleRate = 0.0;
    int blockSize = 512;
    std::map<model::ObjectId, std::shared_ptr<engine::fx::EffectInstance>> instances;
    std::map<ChainOwner, std::vector<model::ObjectId>> orders; // as last built
    std::map<model::ObjectId, std::uint32_t> revivals;
    std::vector<model::ObjectId> used;
    std::set<model::ObjectId> moved;
};

} // namespace daw::controller
