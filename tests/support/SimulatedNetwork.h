#pragma once

#include "daw/net/Link.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace daw::test
{

/**
    A network for tests, on a clock of its own: links whose frames arrive after a random
    delay (so they overtake each other), may be lost or arrive twice, and break when told.
    Deterministic for a seed. Nothing moves until advance().
*/
class SimulatedNetwork
{
public:
    struct Conditions
    {
        double minimumDelay = 0.005; ///< seconds
        double maximumDelay = 0.03;
        double loss = 0.0;        ///< probability that a frame is lost
        double duplication = 0.0; ///< probability that it arrives twice
        bool ordered = false;     ///< like TCP: never overtaken by the one sent after it
    };

    struct Pair
    {
        std::unique_ptr<net::Link> client;
        std::unique_ptr<net::Link> server;
    };

    explicit SimulatedNetwork(juce::int64 seed);
    ~SimulatedNetwork();

    SimulatedNetwork(const SimulatedNetwork&) = delete;
    SimulatedNetwork& operator=(const SimulatedNetwork&) = delete;

    [[nodiscard]] double now() const noexcept;

    /** Moves the clock on; frames due by then arrive. */
    void advance(double seconds);

    /** Two connected ends, open at once. */
    [[nodiscard]] Pair connect(Conditions conditions);

    /** Breaks every link: what is in flight is lost; both ends say why. */
    void cutAll();

    /** Frames sent so far and their bytes (for bandwidth figures). */
    [[nodiscard]] std::uint64_t getFramesSent() const noexcept;
    [[nodiscard]] std::uint64_t getBytesSent() const noexcept;

    struct Impl;

private:
    std::shared_ptr<Impl> impl;
};

} // namespace daw::test
