#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <utility>
#include <vector>

namespace daw::model
{

/**
    One effect of a chain: its type (as the engine knows it, e.g. "equaliser"),
    whether it is bypassed, its parameter values by parameter id (in their units:
    Hz, dB, ms...), the files it plays (a sampler's) and, for effects that have it,
    opaque extra state (a plugin's). Instruments are effects too.
    A handle onto part of a Project's state.
*/
class Effect
{
public:
    explicit Effect(juce::ValueTree effectState);

    [[nodiscard]] ObjectId getId() const;
    [[nodiscard]] juce::String getType() const;

    [[nodiscard]] bool isBypassed() const;
    void setBypassed(bool shouldBeBypassed, juce::UndoManager* undoManager = nullptr);

    /** Nothing for a parameter that was never set (it has its default). Values are
        kept at the precision of a float, as effects use them, so that project files
        hold them exactly. */
    [[nodiscard]] std::optional<double> getParameter(const juce::String& parameterId) const;
    void setParameter(const juce::String& parameterId, double value,
                      juce::UndoManager* undoManager = nullptr);

    /** Every parameter set, in the order they were first set. */
    [[nodiscard]] std::vector<std::pair<juce::String, double>> getParameters() const;

    [[nodiscard]] juce::MemoryBlock getExtraState() const;
    void setExtraState(const juce::MemoryBlock& state, juce::UndoManager* undoManager = nullptr);

    /** Files the effect plays (a sampler's samples), by key ("sample", "pad1"...):
        absolute paths (project files store them relative when they can). Keys are
        made of letters and digits. */
    [[nodiscard]] std::vector<std::pair<juce::String, juce::File>> getFiles() const;
    [[nodiscard]] juce::File getFile(const juce::String& key) const;
    /** An empty file removes the key. */
    void setFile(const juce::String& key, const juce::File& file,
                 juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

/**
    The effects of a track, a bus or the master, in processing order. Removing an
    effect also removes the automation of its parameters.
*/
class EffectList
{
public:
    explicit EffectList(juce::ValueTree listState);

    [[nodiscard]] int getNumEffects() const;
    [[nodiscard]] Effect getEffect(int index) const;
    [[nodiscard]] int indexOf(ObjectId effectId) const; ///< -1 if not in this list

    /** Inserts an effect at index (clamped; the end for -1), with an id no other
        object of the project has, and these parameter values. */
    Effect add(const juce::String& type, int index,
               const std::vector<std::pair<juce::String, double>>& parameters = {},
               juce::UndoManager* undoManager = nullptr);

    void remove(int index, juce::UndoManager* undoManager = nullptr);

    /** Moves the effect at from to index to (both within the list). */
    void move(int from, int to, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
