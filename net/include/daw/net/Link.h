#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace daw::net
{

/**
    One end of a connection that carries frames (blocks of bytes). A TLS connection keeps
    them whole and in order until it closes; a simulated one (tests) may lose, repeat or
    reorder them, which is why co-op sessions put a ReliableChannel on top.

    Polled: its owner sends and collects frames from one thread (the message thread of the
    app, the server's loop).
*/
class Link
{
public:
    enum class State
    {
        connecting,
        open,
        closed
    };

    virtual ~Link() = default;

    /** Sends a frame; frames sent before the link is open wait, after it closed are lost. */
    virtual void send(juce::MemoryBlock frame) = 0;

    /** The frames that arrived since the last call, oldest first. */
    [[nodiscard]] virtual std::vector<juce::MemoryBlock> receive() = 0;

    [[nodiscard]] virtual State getState() const = 0;

    /** Why the link closed (or failed to open), written for the user; empty when it was
        closed on purpose. */
    [[nodiscard]] virtual juce::String getProblem() const = 0;

    /** Closes the link; frames not sent yet are lost. */
    virtual void close() = 0;
};

} // namespace daw::net
