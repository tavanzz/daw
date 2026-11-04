#include "daw/engine/dsp/Envelope.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::dsp
{

namespace
{
// Per sample, what shrinks a distance by 60 dB (a factor of 1000) in seconds.
double factorFor(double seconds, double sampleRate) noexcept
{
    const double samples = std::max(1.0, seconds * sampleRate);
    return std::pow(10.0, -3.0 / samples);
}
} // namespace

void Envelope::setTimes(double attackSeconds, double decaySeconds, double sustainLevel,
                        double releaseSeconds, double sampleRate) noexcept
{
    attackStep = 1.0 / std::max(1.0, attackSeconds * sampleRate);
    decayFactor = factorFor(decaySeconds, sampleRate);
    sustain = std::clamp(sustainLevel, 0.0, 1.0);
    releaseFactor = factorFor(releaseSeconds, sampleRate);
}

void Envelope::noteOff() noexcept
{
    if (stage != Stage::idle)
        stage = Stage::release;
}

void Envelope::reset() noexcept
{
    stage = Stage::idle;
    level = 0.0;
}

double Envelope::next() noexcept
{
    switch (stage)
    {
    case Stage::idle:
        return 0.0;
    case Stage::attack:
        level += attackStep;
        if (level >= 1.0)
        {
            level = 1.0;
            stage = Stage::decay;
        }
        return level;
    case Stage::decay:
        level = sustain + (level - sustain) * decayFactor;
        if (std::abs(level - sustain) < endLevel)
        {
            level = sustain;
            stage = sustain > 0.0 ? Stage::sustain : Stage::idle;
        }
        return level;
    case Stage::sustain:
        level = sustain; // follows a sustain that changes
        return level;
    case Stage::release:
        level *= releaseFactor;
        if (level < endLevel)
        {
            level = 0.0;
            stage = Stage::idle;
        }
        return level;
    }
    return level;
}

void Envelope::skip(int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        next();
}

} // namespace daw::engine::dsp
