#pragma once

#include "daw/engine/Engine.h"

#include <optional>

namespace daw::controller
{

/**
    The engine's stream sample whose sound leaves this computer's speakers at hostTime
    (midi::hostSeconds()'s clock), for "Play for Everyone": the block that starts at
    clock.stream, computed at clock.time, is heard about one block later plus the device's
    output latency. Nothing while the device gives no times (no device, offline).
*/
[[nodiscard]] std::optional<juce::int64> streamSampleHeardAt(const engine::Engine::StreamClock& clock,
                                                            double hostTime,
                                                            int outputLatencySamples,
                                                            int blockSize);

} // namespace daw::controller
