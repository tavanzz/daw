#include "daw/controller/SharedTransport.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw;

TEST_CASE("The stream sample heard at a moment counts the block and the output latency",
          "[controller][coop]")
{
    engine::Engine::StreamClock clock;
    clock.stream = 48000;
    clock.time = 100.0;
    clock.sampleRate = 48000.0;

    // A second after the block came: 48000 samples on, less what lies between computing a
    // sample and hearing it.
    CHECK(controller::streamSampleHeardAt(clock, 101.0, 256, 512) == 48000 + 48000 - 512 - 256);
    CHECK(controller::streamSampleHeardAt(clock, 100.0, 0, 0) == 48000);

    clock.time = 0.0; // no device, no times
    CHECK_FALSE(controller::streamSampleHeardAt(clock, 101.0, 256, 512).has_value());
}
