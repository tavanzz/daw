#pragma once

#include "daw/engine/fx/Effect.h"

#include <memory>
#include <vector>

namespace daw::engine::fx
{

/** An effect type that comes with the app. */
struct BuiltInEffect
{
    const char* type; ///< EffectDescription::type, as project files store it
    const char* name;
};

/** Every built-in effect, in the order a menu offers them. */
[[nodiscard]] const std::vector<BuiltInEffect>& builtInEffects();

/** Every built-in instrument, in the order a menu offers them. */
[[nodiscard]] const std::vector<BuiltInEffect>& builtInInstruments();

/** A new effect or instrument of a built-in type, not prepared; null for an unknown
    type. Message thread (it allocates). */
[[nodiscard]] std::unique_ptr<Effect> createBuiltInEffect(const juce::String& type);

} // namespace daw::engine::fx
