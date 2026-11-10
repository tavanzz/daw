#pragma once

#include "daw/net/Link.h"
#include "daw/net/Tls.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace daw::net
{

namespace detail
{
class TlsConnection;
struct TlsServerContext;
} // namespace detail

/** Limits and timings of a TLS link. */
struct LinkOptions
{
    std::size_t maximumFrameSize = std::size_t{256} << 20;  ///< larger ones close the link
    std::size_t maximumQueuedBytes = std::size_t{1} << 30; ///< unsent bytes before giving up
    double connectTimeout = 10.0; ///< seconds to reach the server
    double handshakeTimeout = 15.0;
    double keepAliveInterval = 5.0; ///< an empty frame when nothing else went out
    double silenceTimeout = 20.0;   ///< nothing received for this long: the peer is gone
};

/**
    A TLS 1.3 connection to a co-op server, opened on a thread of its own. The server's
    certificate is not checked against certificate authorities: the link opens only if the
    SHA-256 of its public key is the fingerprint given (ServerIdentity::getFingerprint(),
    carried by the invite link), and fails otherwise.

    Frames go out and come in on the connection's thread; send() and receive() never
    block.
*/
class TlsClientLink final : public Link
{
public:
    TlsClientLink(const juce::String& host, int port, const juce::String& fingerprint,
                  LinkOptions options = {});
    ~TlsClientLink() override;

    TlsClientLink(const TlsClientLink&) = delete;
    TlsClientLink& operator=(const TlsClientLink&) = delete;

    void send(juce::MemoryBlock frame) override;
    [[nodiscard]] std::vector<juce::MemoryBlock> receive() override;
    [[nodiscard]] State getState() const override;
    [[nodiscard]] juce::String getProblem() const override;
    void close() override;

private:
    std::unique_ptr<detail::TlsConnection> connection;
};

/**
    Accepts TLS 1.3 connections with a ServerIdentity, each on a thread of its own, at
    most maximumConnections at once (more are closed at once). The owner collects new
    connections and polls their links from its own loop, which getActivity() wakes when a
    connection arrives, a frame comes in or a link closes.
*/
class TlsServer
{
public:
    struct Connection
    {
        std::unique_ptr<Link> link;
        juce::String address; ///< the peer's IP address
    };

    TlsServer(ServerIdentity identity, LinkOptions options = {}, int maximumConnections = 64);
    ~TlsServer();

    TlsServer(const TlsServer&) = delete;
    TlsServer& operator=(const TlsServer&) = delete;

    /** Listens on port (0: any free one, see getPort()) of localAddress (empty: every
        interface). */
    [[nodiscard]] juce::Result start(int port, const juce::String& localAddress = {});
    void stop();

    [[nodiscard]] int getPort() const noexcept { return port; }
    [[nodiscard]] juce::String getFingerprint() const { return identity.getFingerprint(); }

    [[nodiscard]] std::vector<Connection> takeNewConnections();

    [[nodiscard]] juce::WaitableEvent& getActivity() noexcept { return *activity; }

private:
    class Listener;

    ServerIdentity identity;
    LinkOptions options;
    int maximumConnections;
    int port = 0;
    std::shared_ptr<juce::WaitableEvent> activity;
    std::shared_ptr<detail::TlsServerContext> context;
    std::unique_ptr<Listener> listener;
};

} // namespace daw::net
