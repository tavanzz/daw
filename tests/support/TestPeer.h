#pragma once

#include "daw/coop/Messages.h"
#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"
#include "daw/net/Link.h"
#include "daw/net/ReliableChannel.h"

#include "SimulatedNetwork.h"

#include <memory>
#include <optional>
#include <vector>

namespace daw::test
{

/**
    A participant reduced to the protocol, for testing the server: it says hello, then sends
    and collects messages through a reliable channel over a simulated link.
*/
class TestPeer
{
public:
    TestPeer(SimulatedNetwork& networkToUse, SimulatedNetwork::Conditions conditions = {})
        : network(networkToUse), linkConditions(conditions)
    {
    }

    /** A new link to the server; returns the server's end, to hand to the server. */
    std::unique_ptr<net::Link> connect()
    {
        auto pair = network.connect(linkConditions);
        link = std::move(pair.client);
        welcome.reset();
        refused.reset();
        early.clear();
        return std::move(pair.server);
    }

    void hello(decltype(coop::Hello::request) request,
               std::uint32_t protocol = static_cast<std::uint32_t>(model::networkProtocolVersion),
               std::uint32_t format = static_cast<std::uint32_t>(model::currentFormatVersion))
    {
        coop::Hello message;
        message.protocolVersion = protocol;
        message.formatVersion = format;
        message.appVersion = "test";
        message.request = std::move(request);
        message.delivered = channel.getDelivered();
        link->send(coop::encodeFrame(message));
    }

    void rejoin()
    {
        hello(coop::RejoinSession{sessionId, number, token});
    }

    void send(const coop::Message& message)
    {
        if (coop::isUnreliable(message))
            channel.sendUnreliable(coop::encode(message));
        else
            channel.send(coop::encode(message));
    }

    /** Moves frames both ways; call between server updates. */
    void pump()
    {
        if (link == nullptr)
            return;
        for (const auto& frame : link->receive())
        {
            const auto kind = coop::frameKindOf(frame);
            if (kind == coop::FrameKind::welcome)
            {
                welcome = coop::decodeWelcome(frame);
                if (welcome)
                {
                    sessionId = welcome->sessionId;
                    number = welcome->participant;
                    if (welcome->token.isNotEmpty())
                        token = welcome->token;
                    if (welcome->resumed)
                        channel.resume(welcome->delivered);
                    else
                        channel.reset();
                }
            }
            else if (kind == coop::FrameKind::refused)
                refused = coop::decodeRefused(frame);
            else if (kind == coop::FrameKind::channel)
            {
                // Frames that overtook the welcome wait for it: the server sends them after.
                if (welcome.has_value())
                    channel.handleFrame(coop::channelPayload(frame), network.now());
                else
                    early.push_back(frame);
            }
            if (welcome.has_value())
            {
                for (const auto& waiting : early)
                    channel.handleFrame(coop::channelPayload(waiting), network.now());
                early.clear();
            }
        }
        for (auto& delivery : channel.takeDelivered())
            if (auto message = coop::decodeMessage(delivery.payload))
                messages.push_back(std::move(*message));
        if (welcome.has_value())
            for (auto& frame : channel.takeFrames(network.now()))
                link->send(coop::channelFrame(frame));
    }

    /** Messages of one type received so far (and forgets them). */
    template <typename Type>
    std::vector<Type> take()
    {
        std::vector<Type> found;
        for (auto message = messages.begin(); message != messages.end();)
        {
            if (auto* typed = std::get_if<Type>(&*message))
            {
                found.push_back(std::move(*typed));
                message = messages.erase(message);
            }
            else
                ++message;
        }
        return found;
    }

    template <typename Type>
    std::optional<Type> last()
    {
        auto found = take<Type>();
        if (found.empty())
            return std::nullopt;
        return found.back();
    }

    void dropConnection()
    {
        if (link != nullptr)
            link->close();
        link.reset();
        welcome.reset();
        early.clear();
    }

    SimulatedNetwork& network;
    SimulatedNetwork::Conditions linkConditions;
    std::unique_ptr<net::Link> link;
    net::ReliableChannel channel;
    std::optional<coop::Welcome> welcome;
    std::optional<coop::Refused> refused;
    std::vector<coop::Message> messages;
    std::vector<juce::MemoryBlock> early;
    juce::String sessionId;
    coop::ParticipantNumber number = 0;
    juce::String token;
};

} // namespace daw::test
