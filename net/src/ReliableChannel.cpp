#include "daw/net/ReliableChannel.h"

namespace daw::net
{

namespace
{

enum class Kind : std::uint8_t
{
    data = 0,
    acknowledgement = 1,
    unreliable = 2
};

void writeVarUInt(juce::MemoryBlock& block, std::uint64_t value)
{
    do
    {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0)
            byte |= 0x80;
        block.append(&byte, 1);
    } while (value != 0);
}

bool readVarUInt(const juce::MemoryBlock& block, std::size_t& position, std::uint64_t& value)
{
    value = 0;
    for (int shift = 0; shift < 64 && position < block.getSize(); shift += 7)
    {
        const auto byte = static_cast<std::uint8_t>(block[position++]);
        value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
            return true;
    }
    return false;
}

juce::MemoryBlock header(Kind kind, std::uint64_t delivered)
{
    juce::MemoryBlock frame;
    const auto byte = static_cast<std::uint8_t>(kind);
    frame.append(&byte, 1);
    writeVarUInt(frame, delivered);
    return frame;
}

} // namespace

ReliableChannel::ReliableChannel()
    : ReliableChannel(Options{})
{
}

ReliableChannel::ReliableChannel(Options channelOptions)
    : options(channelOptions)
{
}

void ReliableChannel::send(juce::MemoryBlock payload)
{
    unacknowledged.push_back({nextNumber++, std::move(payload), -1.0});
}

void ReliableChannel::sendUnreliable(juce::MemoryBlock payload)
{
    unreliable.push_back(std::move(payload));
}

juce::MemoryBlock ReliableChannel::frameFor(const Outgoing& message) const
{
    auto frame = header(Kind::data, delivered);
    writeVarUInt(frame, message.number);
    frame.append(message.payload.getData(), message.payload.getSize());
    return frame;
}

std::vector<juce::MemoryBlock> ReliableChannel::takeFrames(double now)
{
    std::vector<juce::MemoryBlock> frames;

    for (auto& message : unacknowledged)
    {
        if (message.lastSent >= 0.0 && now - message.lastSent < options.retransmitAfter)
            continue;
        frames.push_back(frameFor(message));
        message.lastSent = now;
    }

    for (auto& payload : unreliable)
    {
        auto frame = header(Kind::unreliable, delivered);
        frame.append(payload.getData(), payload.getSize());
        frames.push_back(std::move(frame));
    }
    unreliable.clear();

    if (!frames.empty())
        acknowledgementDue = false; // every frame carries the acknowledgement
    else if (acknowledgementDue && now - acknowledgementDueSince >= options.acknowledgeAfter)
    {
        frames.push_back(header(Kind::acknowledgement, delivered));
        acknowledgementDue = false;
    }

    return frames;
}

bool ReliableChannel::handleFrame(const juce::MemoryBlock& frame, double now)
{
    if (frame.getSize() < 2)
        return false;

    std::size_t position = 1;
    std::uint64_t peerDelivered = 0;
    if (!readVarUInt(frame, position, peerDelivered))
        return false;

    const auto kind = static_cast<Kind>(static_cast<std::uint8_t>(frame[0]));
    switch (kind)
    {
    case Kind::acknowledgement:
        acknowledged(peerDelivered);
        return position == frame.getSize();

    case Kind::unreliable:
        acknowledged(peerDelivered);
        deliveries.push_back(
            {juce::MemoryBlock(static_cast<const char*>(frame.getData()) + position,
                               frame.getSize() - position),
             false});
        return true;

    case Kind::data:
    {
        std::uint64_t number = 0;
        if (!readVarUInt(frame, position, number) || number == 0)
            return false;
        acknowledged(peerDelivered);

        if (!acknowledgementDue)
            acknowledgementDueSince = now;
        acknowledgementDue = true; // even for a repeat: our acknowledgement may have been lost

        if (number <= delivered || early.count(number) > 0)
            return true;

        juce::MemoryBlock payload(static_cast<const char*>(frame.getData()) + position,
                                  frame.getSize() - position);
        if (number != delivered + 1)
        {
            if (early.size() < options.maximumEarly)
                early.emplace(number, std::move(payload));
            return true;
        }

        deliveries.push_back({std::move(payload), true});
        ++delivered;
        for (auto next = early.find(delivered + 1); next != early.end();
             next = early.find(delivered + 1))
        {
            deliveries.push_back({std::move(next->second), true});
            early.erase(next);
            ++delivered;
        }
        return true;
    }
    }

    return false;
}

void ReliableChannel::acknowledged(std::uint64_t peerDelivered)
{
    while (!unacknowledged.empty() && unacknowledged.front().number <= peerDelivered)
        unacknowledged.pop_front();
}

std::vector<ReliableChannel::Delivery> ReliableChannel::takeDelivered()
{
    return std::exchange(deliveries, {});
}

void ReliableChannel::resume(std::uint64_t peerDelivered)
{
    acknowledged(peerDelivered);
    for (auto& message : unacknowledged)
        message.lastSent = -1.0;
    acknowledgementDue = true;
    acknowledgementDueSince = -1.0e9;
}

void ReliableChannel::reset()
{
    nextNumber = 1;
    unacknowledged.clear();
    unreliable.clear();
    delivered = 0;
    early.clear();
    deliveries.clear();
    acknowledgementDue = false;
}

} // namespace daw::net
