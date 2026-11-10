#include "daw/net/FrameReader.h"
#include "daw/net/ReliableChannel.h"
#include "daw/net/Tls.h"
#include "daw/net/TlsLink.h"

#include "../support/SimulatedNetwork.h"

#include <catch2/catch_test_macros.hpp>

#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#include <functional>

using namespace daw;

namespace
{

juce::MemoryBlock textBlock(const juce::String& text)
{
    return {text.toRawUTF8(), text.getNumBytesAsUTF8()};
}

juce::String textOf(const juce::MemoryBlock& block)
{
    return block.toString();
}

// Real sockets: waits (a little at a time) for a condition, up to a limit.
bool waitFor(const std::function<bool()>& condition, double seconds = 10.0)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (!condition())
    {
        if (juce::Time::getMillisecondCounterHiRes() > deadline)
            return false;
        juce::Thread::sleep(2);
    }
    return true;
}

const net::ServerIdentity& testIdentity()
{
    static const auto identity = net::ServerIdentity::create();
    REQUIRE(identity.has_value());
    return *identity;
}

} // namespace

TEST_CASE("Frames are cut out of a stream however it arrives", "[net]")
{
    juce::MemoryBlock stream;
    for (const auto* text : {"one", "", "three", "a longer frame"})
    {
        const auto bytes = net::streamBytesOf(textBlock(text));
        stream.append(bytes.getData(), bytes.getSize());
    }

    for (const std::size_t piece : {std::size_t{1}, std::size_t{3}, std::size_t{7}, stream.getSize()})
    {
        net::FrameReader reader(1000);
        std::vector<juce::MemoryBlock> frames;
        for (std::size_t position = 0; position < stream.getSize(); position += piece)
        {
            REQUIRE(reader.add(static_cast<const char*>(stream.getData()) + position,
                               std::min(piece, stream.getSize() - position)));
            for (auto& frame : reader.takeFrames())
                frames.push_back(std::move(frame));
        }
        // The empty frame keeps a connection alive and is not passed on.
        REQUIRE(frames.size() == 3);
        CHECK(textOf(frames[0]) == "one");
        CHECK(textOf(frames[2]) == "a longer frame");
    }

    net::FrameReader small(10);
    const auto big = net::streamBytesOf(juce::MemoryBlock(11, true));
    CHECK_FALSE(small.add(big.getData(), big.getSize()));
}

TEST_CASE("A reliable channel delivers everything once and in order over a bad network",
          "[net]")
{
    for (const juce::int64 seed : {1, 2, 3})
    {
        test::SimulatedNetwork network(seed);
        const test::SimulatedNetwork::Conditions bad{0.005, 0.3, 0.2, 0.1};
        auto pair = network.connect(bad);
        net::ReliableChannel left, right;
        std::vector<juce::String> sentLeft, sentRight, gotLeft, gotRight;
        juce::Random random(seed);

        const auto pump = [&]
        {
            for (auto& frame : left.takeFrames(network.now()))
                pair.client->send(std::move(frame));
            for (auto& frame : right.takeFrames(network.now()))
                pair.server->send(std::move(frame));
            for (const auto& frame : pair.client->receive())
                REQUIRE(left.handleFrame(frame, network.now()));
            for (const auto& frame : pair.server->receive())
                REQUIRE(right.handleFrame(frame, network.now()));
            for (auto& delivery : left.takeDelivered())
                if (delivery.reliable)
                    gotLeft.push_back(textOf(delivery.payload));
            for (auto& delivery : right.takeDelivered())
                if (delivery.reliable)
                    gotRight.push_back(textOf(delivery.payload));
        };

        for (int step = 0; step < 3000; ++step)
        {
            if (random.nextInt(3) == 0)
            {
                sentLeft.push_back("L" + juce::String(step));
                left.send(textBlock(sentLeft.back()));
            }
            if (random.nextInt(4) == 0)
            {
                sentRight.push_back("R" + juce::String(step));
                right.send(textBlock(sentRight.back()));
            }
            if (random.nextInt(10) == 0)
                left.sendUnreliable(textBlock("presence"));

            pump();
            network.advance(0.01);

            if (random.nextInt(400) == 0)
            {
                // The connection drops; each side tells the other how far it got.
                network.cutAll();
                pair = network.connect(bad);
                left.resume(right.getDelivered());
                right.resume(left.getDelivered());
            }
        }

        // A healthy connection lets everything through.
        network.cutAll();
        pair = network.connect({});
        left.resume(right.getDelivered());
        right.resume(left.getDelivered());
        for (int step = 0; step < 500; ++step)
        {
            pump();
            network.advance(0.01);
        }

        CHECK(gotRight == sentLeft);
        CHECK(gotLeft == sentRight);
        CHECK(left.getUnacknowledged() == 0);
        CHECK(right.getUnacknowledged() == 0);
    }
}

TEST_CASE("A reliable channel refuses malformed frames", "[net]")
{
    net::ReliableChannel channel;
    CHECK_FALSE(channel.handleFrame({}, 0.0));
    CHECK_FALSE(channel.handleFrame(juce::MemoryBlock("\x07\x01", 2), 0.0));
    CHECK_FALSE(channel.handleFrame(juce::MemoryBlock("\x00\x00\x00", 3), 0.0)); // number 0
    CHECK_FALSE(channel.handleFrame(juce::MemoryBlock("\x01\xff\xff", 3), 0.0));
}

TEST_CASE("A server identity has a stable fingerprint", "[net]")
{
    const auto& identity = testIdentity();
    const auto fingerprint = identity.getFingerprint();
    CHECK(fingerprint.length() == 43);
    CHECK(identity.certificatePem.startsWith("-----BEGIN CERTIFICATE-----"));
    CHECK(net::fingerprintOfCertificate("not a certificate").isEmpty());

    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("daw-identity-test-" + juce::String(juce::Random().nextInt()));
    const auto made = net::ServerIdentity::loadOrCreate(folder);
    REQUIRE(made.has_value());
    const auto loaded = net::ServerIdentity::loadOrCreate(folder);
    REQUIRE(loaded.has_value());
#if !JUCE_WINDOWS
    // The key is readable by its owner only.
    struct stat info{};
    REQUIRE(stat(folder.getChildFile("private-key.pem").getFullPathName().toRawUTF8(), &info) == 0);
    CHECK((info.st_mode & 0077) == 0);
#endif
    CHECK(loaded->getFingerprint() == made->getFingerprint());
    CHECK(made->getFingerprint() != fingerprint);
    folder.deleteRecursively();

    const auto again = net::fromBase64Url(fingerprint);
    REQUIRE(again.has_value());
    CHECK(again->getSize() == 32);
    CHECK_FALSE(net::fromBase64Url("not base64!").has_value());

    // SHA-256 of "abc" (FIPS 180-2).
    const auto hash = net::sha256("abc", 3);
    CHECK(juce::String::toHexString(hash.getData(), static_cast<int>(hash.getSize()), 0)
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("Frames cross a TLS connection both ways", "[net][tls]")
{
    net::TlsServer server(testIdentity());
    REQUIRE(server.start(0, "127.0.0.1").wasOk());
    REQUIRE(server.getPort() > 0);

    net::TlsClientLink client("127.0.0.1", server.getPort(), server.getFingerprint());
    std::vector<net::TlsServer::Connection> accepted;
    REQUIRE(waitFor(
        [&]
        {
            for (auto& connection : server.takeNewConnections())
                accepted.push_back(std::move(connection));
            return !accepted.empty() && client.getState() == net::Link::State::open;
        }));
    CHECK(accepted[0].address == "127.0.0.1");
    auto& serverEnd = *accepted[0].link;

    // Many small frames, then a large one, both ways at once.
    juce::MemoryBlock large(5 * 1024 * 1024);
    juce::Random(3).fillBitsRandomly(large.getData(), large.getSize());
    for (int index = 0; index < 200; ++index)
    {
        client.send(textBlock("to server " + juce::String(index)));
        serverEnd.send(textBlock("to client " + juce::String(index)));
    }
    client.send(large);
    serverEnd.send(large);

    std::vector<juce::MemoryBlock> atServer, atClient;
    REQUIRE(waitFor(
        [&]
        {
            for (auto& frame : serverEnd.receive())
                atServer.push_back(std::move(frame));
            for (auto& frame : client.receive())
                atClient.push_back(std::move(frame));
            return atServer.size() == 201 && atClient.size() == 201;
        },
        30.0));
    CHECK(textOf(atServer[0]) == "to server 0");
    CHECK(textOf(atClient[199]) == "to client 199");
    CHECK(atServer[200] == large);
    CHECK(atClient[200] == large);

    // Closing one end closes the other.
    client.close();
    REQUIRE(waitFor([&] { return serverEnd.getState() == net::Link::State::closed; }));
    CHECK(client.getProblem().isEmpty());
}

TEST_CASE("A client refuses a server whose identity differs from the invite", "[net][tls]")
{
    net::TlsServer server(testIdentity());
    REQUIRE(server.start(0, "127.0.0.1").wasOk());

    const auto other = net::ServerIdentity::create();
    REQUIRE(other.has_value());
    net::TlsClientLink client("127.0.0.1", server.getPort(), other->getFingerprint());
    REQUIRE(waitFor([&] { return client.getState() == net::Link::State::closed; }));
    CHECK(client.getProblem().contains("identity differs"));
}

TEST_CASE("A server survives connections that do not speak TLS", "[net][tls]")
{
    net::LinkOptions quick;
    quick.handshakeTimeout = 1.0;
    net::TlsServer server(testIdentity(), quick);
    REQUIRE(server.start(0, "127.0.0.1").wasOk());

    {
        juce::StreamingSocket plain;
        REQUIRE(plain.connect("127.0.0.1", server.getPort(), 2000));
        const juce::String request("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        CHECK(plain.write(request.toRawUTF8(), static_cast<int>(request.getNumBytesAsUTF8())) > 0);
        std::vector<net::TlsServer::Connection> accepted;
        REQUIRE(waitFor(
            [&]
            {
                for (auto& connection : server.takeNewConnections())
                    accepted.push_back(std::move(connection));
                return !accepted.empty()
                       && accepted[0].link->getState() == net::Link::State::closed;
            }));
    }

    net::TlsClientLink client("127.0.0.1", server.getPort(), server.getFingerprint());
    REQUIRE(waitFor([&] { return client.getState() == net::Link::State::open; }));
}

TEST_CASE("A client that cannot reach the server says so", "[net][tls]")
{
    int freePort = 0;
    {
        net::TlsServer server(testIdentity());
        REQUIRE(server.start(0, "127.0.0.1").wasOk());
        freePort = server.getPort();
    }

    net::LinkOptions quick;
    quick.connectTimeout = 1.0;
    net::TlsClientLink client("127.0.0.1", freePort, "x", quick);
    REQUIRE(waitFor([&] { return client.getState() == net::Link::State::closed; }));
    CHECK(client.getProblem().startsWith("Could not reach 127.0.0.1:"));
}
