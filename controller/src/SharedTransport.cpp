#include "daw/controller/SharedTransport.h"

#include <cmath>

namespace daw::controller
{

std::optional<juce::int64> streamSampleHeardAt(const engine::Engine::StreamClock& clock,
                                              double hostTime, int outputLatencySamples,
                                              int blockSize)
{
    if (clock.time <= 0.0 || clock.sampleRate <= 0.0)
        return std::nullopt;

    const auto fromBlock = static_cast<juce::int64>(std::llround((hostTime - clock.time) * clock.sampleRate));
    return clock.stream + fromBlock - blockSize - outputLatencySamples;
}

} // namespace daw::controller
