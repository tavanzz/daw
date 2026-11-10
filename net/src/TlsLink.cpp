#include "daw/net/TlsLink.h"

#include "daw/net/FrameReader.h"

#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace daw::net
{

namespace detail
{

namespace
{

double now()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

std::vector<unsigned char> nullTerminated(const juce::String& text)
{
    const auto utf8 = text.toUTF8();
    return {utf8.getAddress(), utf8.getAddress() + utf8.sizeInBytes()};
}

void configureTls13(mbedtls_ssl_config& config)
{
    mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_3);
    // Reads wait for our own short timeouts (TlsConnection::receiveBytes()).
    mbedtls_ssl_conf_read_timeout(&config, 5);
}

} // namespace

/** The server's TLS configuration, shared by its connections (read only once made). */
struct TlsServerContext
{
    TlsServerContext()
    {
        mbedtls_ssl_config_init(&config);
        mbedtls_x509_crt_init(&certificate);
        mbedtls_pk_init(&key);
    }

    ~TlsServerContext()
    {
        mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&certificate);
        mbedtls_pk_free(&key);
    }

    TlsServerContext(const TlsServerContext&) = delete;
    TlsServerContext& operator=(const TlsServerContext&) = delete;

    bool setUp(const ServerIdentity& identity)
    {
        const auto certificatePem = nullTerminated(identity.certificatePem);
        const auto keyPem = nullTerminated(identity.privateKeyPem);
        if (mbedtls_x509_crt_parse(&certificate, certificatePem.data(), certificatePem.size()) != 0
            || mbedtls_pk_parse_key(&key, keyPem.data(), keyPem.size(), nullptr, 0) != 0
            || mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_SERVER,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)
                   != 0
            || mbedtls_ssl_conf_own_cert(&config, &certificate, &key) != 0)
            return false;

        configureTls13(config);
        return true;
    }

    mbedtls_ssl_config config;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
};

/**
    One TLS connection, client or server side, run by a thread of its own: it connects
    (client), shakes hands, checks the server's fingerprint (client), then writes queued
    frames and reads incoming ones, alternating so that two peers writing much at once
    never wait for each other.
*/
class TlsConnection : private juce::Thread
{
public:
    // A client connection.
    TlsConnection(juce::String hostToReach, int portToReach, juce::String expectedFingerprint,
                  LinkOptions linkOptions)
        : juce::Thread("Co-op connection"),
          options(linkOptions),
          host(std::move(hostToReach)),
          port(portToReach),
          fingerprint(std::move(expectedFingerprint)),
          frameReader(options.maximumFrameSize)
    {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&clientConfig);
        startThread();
    }

    // A server connection, accepted by the listener.
    TlsConnection(std::unique_ptr<juce::StreamingSocket> acceptedSocket,
                  std::shared_ptr<TlsServerContext> context,
                  std::shared_ptr<juce::WaitableEvent> activityEvent,
                  std::shared_ptr<std::atomic<int>> connectionCount, LinkOptions linkOptions)
        : juce::Thread("Co-op connection"),
          options(linkOptions),
          socket(std::move(acceptedSocket)),
          serverContext(std::move(context)),
          activity(std::move(activityEvent)),
          count(std::move(connectionCount)),
          frameReader(options.maximumFrameSize)
    {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&clientConfig);
        ++*count;
        startThread();
    }

    ~TlsConnection() override
    {
        close();
        stopThread(static_cast<int>(connectAttempt * 1000.0) + 2000);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&clientConfig);
        if (count != nullptr)
            --*count;
    }

    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;

    void send(juce::MemoryBlock frame)
    {
        const std::lock_guard lock(mutex);
        if (state == Link::State::closed)
            return;
        queuedBytes += frame.getSize();
        outgoing.push_back(std::move(frame));
    }

    std::vector<juce::MemoryBlock> receive()
    {
        const std::lock_guard lock(mutex);
        return std::exchange(incoming, {});
    }

    Link::State getState() const
    {
        const std::lock_guard lock(mutex);
        return state;
    }

    juce::String getProblem() const
    {
        const std::lock_guard lock(mutex);
        return problem;
    }

    void close()
    {
        signalThreadShouldExit();
        const std::lock_guard lock(mutex);
        if (state != Link::State::closed)
            closedOnPurpose = true;
    }

private:
    static constexpr double connectAttempt = 2.0;

    void run() override
    {
        if (socket == nullptr && !connect())
            return;
        if (!shakeHands())
            return;

        {
            const std::lock_guard lock(mutex);
            state = Link::State::open;
        }
        exchangeFrames();
    }

    bool connect()
    {
        const auto deadline = now() + options.connectTimeout;
        while (!threadShouldExit())
        {
            auto attempt = std::make_unique<juce::StreamingSocket>();
            if (attempt->connect(host, port, static_cast<int>(connectAttempt * 1000.0)))
            {
                socket = std::move(attempt);
                return true;
            }
            if (now() >= deadline)
                break;
        }

        finish("Could not reach " + host + ":" + juce::String(port) + ".");
        return false;
    }

    bool shakeHands()
    {
        if (serverContext != nullptr)
        {
            if (mbedtls_ssl_setup(&ssl, &serverContext->config) != 0)
                return finish("The secure connection could not be set up.");
        }
        else
        {
            if (mbedtls_ssl_config_defaults(&clientConfig, MBEDTLS_SSL_IS_CLIENT,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT)
                != 0)
                return finish("The secure connection could not be set up.");
            configureTls13(clientConfig);
            // The certificate is checked against the fingerprint below, not against
            // certificate authorities (the handshake proves the server holds its key).
            mbedtls_ssl_conf_authmode(&clientConfig, MBEDTLS_SSL_VERIFY_NONE);
            if (mbedtls_ssl_setup(&ssl, &clientConfig) != 0)
                return finish("The secure connection could not be set up.");
        }

        mbedtls_ssl_set_bio(&ssl, this, sendBytes, nullptr, receiveBytes);

        const auto deadline = now() + options.handshakeTimeout;
        for (;;)
        {
            const auto result = mbedtls_ssl_handshake(&ssl);
            if (result == 0)
                break;
            if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE
                && result != MBEDTLS_ERR_SSL_TIMEOUT)
                return finish(serverContext != nullptr
                                  ? juce::String("The secure connection failed.")
                                  : "The secure connection to the server failed.");
            if (threadShouldExit())
                return finish({});
            if (now() >= deadline)
                return finish("The server did not answer.");
        }

        if (serverContext == nullptr)
        {
            const auto* certificate = mbedtls_ssl_get_peer_cert(&ssl);
            if (certificate == nullptr)
                return finish("The server did not say who it is.");
            const auto hash = sha256(certificate->pk_raw.p, certificate->pk_raw.len);
            if (toBase64Url(hash.getData(), hash.getSize()) != fingerprint)
                return finish("This server is not the one the invite names (its identity "
                              "differs). Ask for a new invite.");
        }
        return true;
    }

    void exchangeFrames()
    {
        auto lastSent = now();
        auto lastReceived = now();
        juce::MemoryBlock pending;
        std::size_t written = 0;
        std::vector<unsigned char> buffer(65536);

        while (!threadShouldExit())
        {
            if (pending.isEmpty())
                takeOutgoing(pending);

            bool wrote = false;
            if (written < pending.getSize())
            {
                const auto chunk = std::min<std::size_t>(pending.getSize() - written, 65536);
                const auto result = mbedtls_ssl_write(
                    &ssl, static_cast<const unsigned char*>(pending.getData()) + written, chunk);
                if (result > 0)
                {
                    written += static_cast<std::size_t>(result);
                    wrote = true;
                    lastSent = now();
                    if (written == pending.getSize())
                    {
                        pending.reset();
                        written = 0;
                    }
                }
                else if (result != MBEDTLS_ERR_SSL_WANT_WRITE && result != MBEDTLS_ERR_SSL_WANT_READ)
                {
                    finish("The connection broke.");
                    return;
                }
            }

            // Only glance at incoming data while writing goes on.
            readTimeoutMilliseconds = wrote ? 0 : 5;
            const auto result = mbedtls_ssl_read(&ssl, buffer.data(), buffer.size());
            if (result > 0)
            {
                lastReceived = now();
                if (!frameReader.add(buffer.data(), static_cast<std::size_t>(result)))
                {
                    finish("The other side sent something too large.");
                    return;
                }
                deliver(frameReader.takeFrames());
            }
            else if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
                     || result == MBEDTLS_ERR_SSL_CONN_EOF)
            {
                finish(serverContext != nullptr ? juce::String("The participant left.")
                                                : "The server closed the connection.");
                return;
            }
            else if (result != MBEDTLS_ERR_SSL_TIMEOUT && result != MBEDTLS_ERR_SSL_WANT_READ
                     && result != MBEDTLS_ERR_SSL_WANT_WRITE
                     && result != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            {
                finish("The connection broke.");
                return;
            }

            const auto time = now();
            if (pending.isEmpty() && time - lastSent >= options.keepAliveInterval)
            {
                pending = streamBytesOf({});
                written = 0;
            }
            if (time - lastReceived >= options.silenceTimeout)
            {
                finish("The connection stopped answering.");
                return;
            }
            if (tooMuchQueued())
            {
                finish("The other side stopped reading.");
                return;
            }
        }

        mbedtls_ssl_close_notify(&ssl);
        finish({});
    }

    // Many small frames go out together, in one write.
    void takeOutgoing(juce::MemoryBlock& pending)
    {
        const std::lock_guard lock(mutex);
        while (!outgoing.empty() && pending.getSize() < 65536)
        {
            const auto bytes = streamBytesOf(outgoing.front());
            queuedBytes -= outgoing.front().getSize();
            outgoing.pop_front();
            pending.append(bytes.getData(), bytes.getSize());
        }
    }

    bool tooMuchQueued()
    {
        const std::lock_guard lock(mutex);
        return queuedBytes > options.maximumQueuedBytes;
    }

    void deliver(std::vector<juce::MemoryBlock> frames)
    {
        if (frames.empty())
            return;
        {
            const std::lock_guard lock(mutex);
            for (auto& frame : frames)
                incoming.push_back(std::move(frame));
        }
        if (activity != nullptr)
            activity->signal();
    }

    bool finish(const juce::String& reason)
    {
        if (socket != nullptr)
            socket->close();
        {
            const std::lock_guard lock(mutex);
            if (state != Link::State::closed)
            {
                state = Link::State::closed;
                problem = closedOnPurpose ? juce::String() : reason;
                outgoing.clear();
                queuedBytes = 0;
            }
        }
        if (activity != nullptr)
            activity->signal();
        return false;
    }

    // Mbed TLS's input and output, over the socket.
    static int sendBytes(void* context, const unsigned char* data, std::size_t size)
    {
        auto& connection = *static_cast<TlsConnection*>(context);
        auto& socket = *connection.socket;
        const auto ready = socket.waitUntilReady(false, 5);
        if (ready < 0)
            return MBEDTLS_ERR_NET_SEND_FAILED;
        if (ready == 0)
            return MBEDTLS_ERR_SSL_WANT_WRITE;

        const auto written = socket.write(data, static_cast<int>(std::min<std::size_t>(size, 1 << 20)));
        if (written > 0)
            return written;
        // Nothing went: the buffer filled up meanwhile, unless the socket failed.
        return socket.waitUntilReady(false, 0) < 0 ? MBEDTLS_ERR_NET_SEND_FAILED
                                                   : MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    static int receiveBytes(void* context, unsigned char* data, std::size_t size, std::uint32_t)
    {
        auto& connection = *static_cast<TlsConnection*>(context);
        auto& socket = *connection.socket;
        const auto ready = socket.waitUntilReady(true, connection.readTimeoutMilliseconds);
        if (ready < 0)
            return MBEDTLS_ERR_NET_RECV_FAILED;
        if (ready == 0)
            return MBEDTLS_ERR_SSL_TIMEOUT;

        const auto read = socket.read(data, static_cast<int>(std::min<std::size_t>(size, 1 << 20)), false);
        if (read < 0)
            return MBEDTLS_ERR_NET_RECV_FAILED;
        return read; // 0 after "ready": the peer closed the connection
    }

    const LinkOptions options;
    juce::String host;
    int port = 0;
    juce::String fingerprint;

    std::unique_ptr<juce::StreamingSocket> socket;
    std::shared_ptr<TlsServerContext> serverContext;
    std::shared_ptr<juce::WaitableEvent> activity;
    std::shared_ptr<std::atomic<int>> count;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config clientConfig;
    FrameReader frameReader;
    int readTimeoutMilliseconds = 100;

    mutable std::mutex mutex;
    std::deque<juce::MemoryBlock> outgoing;
    std::size_t queuedBytes = 0;
    std::vector<juce::MemoryBlock> incoming;
    Link::State state = Link::State::connecting;
    juce::String problem;
    bool closedOnPurpose = false;
};

} // namespace detail

// TlsClientLink -------------------------------------------------------------------------

TlsClientLink::TlsClientLink(const juce::String& host, int port, const juce::String& fingerprint,
                             LinkOptions options)
{
    initialiseTls();
    connection = std::make_unique<detail::TlsConnection>(host, port, fingerprint, options);
}

TlsClientLink::~TlsClientLink() = default;

void TlsClientLink::send(juce::MemoryBlock frame)
{
    connection->send(std::move(frame));
}

std::vector<juce::MemoryBlock> TlsClientLink::receive()
{
    return connection->receive();
}

Link::State TlsClientLink::getState() const
{
    return connection->getState();
}

juce::String TlsClientLink::getProblem() const
{
    return connection->getProblem();
}

void TlsClientLink::close()
{
    connection->close();
}

// TlsServer -----------------------------------------------------------------------------

namespace
{

class ServerLink final : public Link
{
public:
    explicit ServerLink(std::unique_ptr<detail::TlsConnection> openedConnection)
        : connection(std::move(openedConnection))
    {
    }

    void send(juce::MemoryBlock frame) override { connection->send(std::move(frame)); }
    std::vector<juce::MemoryBlock> receive() override { return connection->receive(); }
    State getState() const override { return connection->getState(); }
    juce::String getProblem() const override { return connection->getProblem(); }
    void close() override { connection->close(); }

private:
    std::unique_ptr<detail::TlsConnection> connection;
};

} // namespace

class TlsServer::Listener : private juce::Thread
{
public:
    Listener(std::unique_ptr<juce::StreamingSocket> listeningSocket,
             std::shared_ptr<detail::TlsServerContext> serverContext,
             std::shared_ptr<juce::WaitableEvent> activityEvent, LinkOptions linkOptions,
             int maximum)
        : juce::Thread("Co-op listener"),
          socket(std::move(listeningSocket)),
          context(std::move(serverContext)),
          activity(std::move(activityEvent)),
          options(linkOptions),
          maximumConnections(maximum)
    {
        startThread();
    }

    ~Listener() override
    {
        signalThreadShouldExit();
        socket->close();
        stopThread(5000);
    }

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    std::vector<Connection> takeNew()
    {
        const std::lock_guard lock(mutex);
        return std::exchange(accepted, {});
    }

private:
    void run() override
    {
        while (!threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> client(socket->waitForNextConnection());
            if (client == nullptr)
            {
                if (!threadShouldExit())
                    wait(100);
                continue;
            }
            if (count->load() >= maximumConnections)
                continue; // closed as it goes

            const auto address = client->getHostName();
            auto connection = std::make_unique<detail::TlsConnection>(std::move(client), context,
                                                                      activity, count, options);
            {
                const std::lock_guard lock(mutex);
                accepted.push_back({std::make_unique<ServerLink>(std::move(connection)), address});
            }
            activity->signal();
        }
    }

    std::unique_ptr<juce::StreamingSocket> socket;
    std::shared_ptr<detail::TlsServerContext> context;
    std::shared_ptr<juce::WaitableEvent> activity;
    std::shared_ptr<std::atomic<int>> count = std::make_shared<std::atomic<int>>(0);
    LinkOptions options;
    int maximumConnections;

    std::mutex mutex;
    std::vector<Connection> accepted;
};

TlsServer::TlsServer(ServerIdentity serverIdentity, LinkOptions linkOptions, int maximum)
    : identity(std::move(serverIdentity)),
      options(linkOptions),
      maximumConnections(maximum),
      activity(std::make_shared<juce::WaitableEvent>())
{
    initialiseTls();
}

TlsServer::~TlsServer()
{
    stop();
}

juce::Result TlsServer::start(int portToListenOn, const juce::String& localAddress)
{
    stop();

    context = std::make_shared<detail::TlsServerContext>();
    if (!context->setUp(identity))
        return juce::Result::fail("The server's certificate or key is not valid.");

    auto socket = std::make_unique<juce::StreamingSocket>();
    if (!socket->createListener(portToListenOn, localAddress))
        return juce::Result::fail("Cannot listen on port " + juce::String(portToListenOn)
                                  + " (in use, or not allowed).");

    port = socket->getBoundPort();
    listener = std::make_unique<Listener>(std::move(socket), context, activity, options,
                                          maximumConnections);
    return juce::Result::ok();
}

void TlsServer::stop()
{
    listener.reset();
    port = 0;
}

std::vector<TlsServer::Connection> TlsServer::takeNewConnections()
{
    return listener != nullptr ? listener->takeNew() : std::vector<Connection>{};
}

} // namespace daw::net
