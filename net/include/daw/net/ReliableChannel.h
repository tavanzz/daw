#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace daw::net
{

/**
    Messages between two peers delivered once each and in order, over links that may lose,
    repeat or reorder frames, or break (a TLS connection that drops loses what was in
    flight). Each reliable message has a number; the receiver delivers them in order, keeps
    early ones aside and drops repeats, and acknowledges how far it got; the sender keeps
    what is not acknowledged and sends it again after a while, or at once on a new link
    (resume()). Unreliable messages (presence, clocks) go once, lost or not.

    Pure logic on a clock it is given: its owner moves frames between it and a Link.
    Frames: a kind byte (0 data, 1 acknowledgement, 2 unreliable), the number of messages
    delivered so far (an acknowledgement), then for data its number and the payload, for
    unreliable ones the payload.
*/
class ReliableChannel
{
public:
    struct Options
    {
        double retransmitAfter = 1.0;  ///< seconds without an acknowledgement
        double acknowledgeAfter = 0.02; ///< when nothing else goes the other way
        std::size_t maximumUnacknowledged = 100'000; ///< messages: beyond, isOverwhelmed()
        std::size_t maximumEarly = 10'000;           ///< messages kept aside out of order
    };

    struct Delivery
    {
        juce::MemoryBlock payload;
        bool reliable = true;
    };

    ReliableChannel();
    explicit ReliableChannel(Options options);

    void send(juce::MemoryBlock payload);
    void sendUnreliable(juce::MemoryBlock payload);

    /** A frame that came from the peer; false if it is malformed. */
    bool handleFrame(const juce::MemoryBlock& frame, double now);

    /** Frames to send now: new messages, messages due again, an acknowledgement. */
    [[nodiscard]] std::vector<juce::MemoryBlock> takeFrames(double now);

    /** Messages received, in order (unreliable ones as they came). */
    [[nodiscard]] std::vector<Delivery> takeDelivered();

    /** A new link to the peer, which has delivered `peerDelivered` messages: everything
        after that goes again at once. */
    void resume(std::uint64_t peerDelivered);

    /** Forget everything, both ways (a new session, or a peer that lost its state). */
    void reset();

    [[nodiscard]] std::uint64_t getDelivered() const noexcept { return delivered; }
    [[nodiscard]] std::uint64_t getSent() const noexcept { return nextNumber - 1; }
    /** Messages the peer has acknowledged: a peer that has fewer cannot resume (they are
        gone from here). */
    [[nodiscard]] std::uint64_t getAcknowledged() const noexcept
    {
        return unacknowledged.empty() ? nextNumber - 1 : unacknowledged.front().number - 1;
    }
    [[nodiscard]] std::size_t getUnacknowledged() const noexcept { return unacknowledged.size(); }
    /** True when too much waits for an acknowledgement: the peer is gone too long. */
    [[nodiscard]] bool isOverwhelmed() const noexcept
    {
        return unacknowledged.size() > options.maximumUnacknowledged;
    }

private:
    struct Outgoing
    {
        std::uint64_t number = 0;
        juce::MemoryBlock payload;
        double lastSent = -1.0; // never
    };

    void acknowledged(std::uint64_t peerDelivered);
    [[nodiscard]] juce::MemoryBlock frameFor(const Outgoing& message) const;

    Options options;
    std::uint64_t nextNumber = 1;
    std::deque<Outgoing> unacknowledged;
    std::vector<juce::MemoryBlock> unreliable;

    std::uint64_t delivered = 0;
    std::map<std::uint64_t, juce::MemoryBlock> early;
    std::vector<Delivery> deliveries;
    bool acknowledgementDue = false;
    double acknowledgementDueSince = 0.0;
};

} // namespace daw::net
