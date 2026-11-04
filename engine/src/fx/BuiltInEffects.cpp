#include "daw/engine/fx/BuiltInEffects.h"

#include "daw/engine/fx/Compressor.h"
#include "daw/engine/fx/Delay.h"
#include "daw/engine/fx/Equaliser.h"
#include "daw/engine/fx/Limiter.h"
#include "daw/engine/fx/Reverb.h"
#include "daw/engine/fx/Sampler.h"
#include "daw/engine/fx/Synth.h"

namespace daw::engine::fx
{

const std::vector<BuiltInEffect>& builtInEffects()
{
    static const std::vector<BuiltInEffect> effects{
        {Equaliser::typeId, "Equaliser"}, {Compressor::typeId, "Compressor"},
        {Limiter::typeId, "Limiter"},     {Delay::typeId, "Delay"},
        {Reverb::typeId, "Reverb"},
    };
    return effects;
}

const std::vector<BuiltInEffect>& builtInInstruments()
{
    static const std::vector<BuiltInEffect> instruments{
        {Synth::typeId, "Synth"},
        {Sampler::typeId, "Sampler"},
    };
    return instruments;
}

std::unique_ptr<Effect> createBuiltInEffect(const juce::String& type)
{
    if (type == Synth::typeId)
        return std::make_unique<Synth>();
    if (type == Sampler::typeId)
        return std::make_unique<Sampler>();
    if (type == Equaliser::typeId)
        return std::make_unique<Equaliser>();
    if (type == Compressor::typeId)
        return std::make_unique<Compressor>();
    if (type == Limiter::typeId)
        return std::make_unique<Limiter>();
    if (type == Delay::typeId)
        return std::make_unique<Delay>();
    if (type == Reverb::typeId)
        return std::make_unique<Reverb>();
    return nullptr;
}

} // namespace daw::engine::fx
