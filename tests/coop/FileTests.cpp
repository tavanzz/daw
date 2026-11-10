#include "daw/coop/Client.h"
#include "daw/net/Tls.h"

#include "../support/CoopWorld.h"
#include "../support/TestPeer.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw;

namespace
{

using Kind = coop::FileSync::State::Kind;

// The state of a file the copy knows (it must know it).
Kind kindOf(const coop::FileSync& files, const juce::String& path)
{
    const auto state = files.stateOf(path);
    REQUIRE(state.has_value());
    return state->kind;
}

juce::MemoryBlock randomBytes(std::size_t size, juce::int64 seed)
{
    juce::MemoryBlock data(size);
    juce::Random(seed).fillBitsRandomly(data.getData(), data.getSize());
    return data;
}

void writeFile(const juce::File& file, const juce::MemoryBlock& data)
{
    file.getParentDirectory().createDirectory();
    REQUIRE(file.replaceWithData(data.getData(), data.getSize()));
}

juce::MemoryBlock contentOf(const juce::File& file)
{
    juce::MemoryBlock data;
    file.loadFileAsData(data);
    return data;
}

struct FileSession
{
    // Links like TCP's (frames in order), unless told otherwise.
    explicit FileSession(server::ServerOptions options = {}, juce::int64 seed = 3,
                         test::SimulatedNetwork::Conditions conditions = {0.005, 0.03, 0.0, 0.0, true})
        : world(seed, std::move(options))
    {
        world.conditions = conditions;
        auto& host = world.add("files-host-" + juce::String(seed));
        host.folder.deleteRecursively();
        host.project.addTrack("Takes");
        host.client->host(test::CoopWorld::hostKey, "Ann");
        for (int wait = 0; wait < 1200 && !host.client->getInvite(); ++wait)
            world.run(0.05);
        REQUIRE(host.client->getInvite().has_value());
        auto& guest = world.add("files-guest-" + juce::String(seed));
        guest.folder.deleteRecursively();
        guest.client->join(host.client->getInvite()->code, "Marco");
        for (int wait = 0; wait < 1200 && !world.isQuiet(); ++wait)
            world.run(0.05);
        REQUIRE(guest.client->getStatus() == coop::Client::Status::connected);
    }

    ~FileSession()
    {
        for (auto& participant : world.participants)
            participant->folder.deleteRecursively();
    }

    test::CoopWorld::Participant& host() { return *world.participants[0]; }
    test::CoopWorld::Participant& guest() { return *world.participants[1]; }

    // As the controller will: the file goes into the copy, is hashed, then a clip refers
    // to it.
    void addTake(const juce::String& path, const juce::MemoryBlock& data)
    {
        writeFile(host().folder.getChildFile(path), data);
        host().client->getFiles().addLocalFile(path);
        for (int wait = 0; wait < 100 && host().client->getFiles().isHashing(); ++wait)
            world.tick();
        REQUIRE_FALSE(host().client->getFiles().isHashing());

        host().client->setTransaction("Add Clip", host().client->newUndoStep());
        host().project.getTrack(0).addClip(host().folder.getChildFile(path), {0, 0, 1000});
        host().client->flush();
    }

    void runUntilFilesArrive(double limit = 60.0)
    {
        for (double time = 0.0; time < limit; time += 0.1)
        {
            world.run(0.1);
            if (host().client->getFiles().isIdle() && guest().client->getFiles().isIdle()
                && world.isQuiet())
                return;
        }
    }

    test::CoopWorld world;
};

} // namespace

TEST_CASE("Files go up once and come down to everyone checked", "[coop][files]")
{
    FileSession session;
    const auto take = randomBytes(3 * 1024 * 1024 + 17, 1);
    session.addTake("Audio/1/take 1.wav", take);

    // On its way: the guest sees the clip at once, its file comes.
    session.world.run(0.3);
    REQUIRE(session.guest().project.getTrack(0).getNumClips() == 1);
    const auto state = session.guest().client->getFiles().stateOf("Audio/1/take 1.wav");
    REQUIRE(state.has_value());
    CHECK(state->kind != Kind::present);
    CHECK(state->size == take.getSize());

    session.runUntilFilesArrive();
    const auto arrived = session.guest().client->getFiles().takeArrived();
    REQUIRE(arrived.size() == 1);
    CHECK(arrived[0] == "Audio/1/take 1.wav");
    CHECK(contentOf(session.guest().folder.getChildFile("Audio/1/take 1.wav")) == take);
    CHECK(kindOf(session.guest().client->getFiles(), "Audio/1/take 1.wav") == Kind::present);
    CHECK(kindOf(session.host().client->getFiles(), "Audio/1/take 1.wav") == Kind::present);
    CHECK_FALSE(session.guest().folder.getChildFile("Audio/1/take 1.wav.part").exists());
    CHECK(session.world.server().getFileBytes(session.world.server().getSessionIds()[0])
          == take.getSize());
}

TEST_CASE("The same content under two names comes down once", "[coop][files]")
{
    FileSession session;
    const auto take = randomBytes(2 * 1024 * 1024, 2);
    session.addTake("Audio/1/a.wav", take);
    session.addTake("Audio/1/copy of a.wav", take);
    session.runUntilFilesArrive();

    CHECK(contentOf(session.guest().folder.getChildFile("Audio/1/a.wav")) == take);
    CHECK(contentOf(session.guest().folder.getChildFile("Audio/1/copy of a.wav")) == take);
    CHECK(session.guest().client->getFiles().getBytesReceived() < take.getSize() * 5 / 4);
    CHECK(session.host().client->getFiles().getBytesSent() < take.getSize() * 5 / 4);
    CHECK(session.world.server().getFileBytes(session.world.server().getSessionIds()[0])
          == take.getSize());
}

TEST_CASE("Transfers resume where they stopped after a broken connection", "[coop][files]")
{
    FileSession session;
    const auto take = randomBytes(8 * 1024 * 1024, 3);
    session.addTake("Audio/1/long take.wav", take);

    // The connections break while the file goes up, and again while it comes down.
    for (int cut = 0; cut < 6; ++cut)
    {
        session.world.run(0.15);
        session.world.network.cutAll();
    }
    session.runUntilFilesArrive(120.0);

    CHECK(contentOf(session.guest().folder.getChildFile("Audio/1/long take.wav")) == take);
    // Nothing went twice, apart from what was in flight at each break.
    CHECK(session.host().client->getFiles().getBytesSent() < take.getSize() * 3 / 2);
    CHECK(session.guest().client->getFiles().getBytesReceived() < take.getSize() * 3 / 2);
}

TEST_CASE("A file the session has no room for is refused", "[coop][files]")
{
    server::ServerOptions options;
    options.maximumSessionBytes = 1024 * 1024;
    FileSession session(options);
    session.addTake("Audio/1/big.wav", randomBytes(2 * 1024 * 1024, 4));
    session.world.run(2.0);

    CHECK(kindOf(session.host().client->getFiles(), "Audio/1/big.wav") == Kind::failed);
    CHECK(session.host().client->getFiles().takeNotices().joinIntoString(" ").contains("larger than the session"));
    CHECK(kindOf(session.guest().client->getFiles(), "Audio/1/big.wav") == Kind::incoming);
}

TEST_CASE("A file already in the copy is checked and not downloaded again", "[coop][files]")
{
    FileSession session(server::ServerOptions{}, 5);
    const auto take = randomBytes(1024 * 1024, 5);
    writeFile(session.guest().folder.getChildFile("Audio/1/known.wav"), take);
    session.addTake("Audio/1/known.wav", take);
    session.runUntilFilesArrive();

    CHECK(kindOf(session.guest().client->getFiles(), "Audio/1/known.wav") == Kind::present);
    CHECK(session.guest().client->getFiles().getBytesReceived() < 64 * 1024);
}

TEST_CASE("The server keeps nothing that does not have its hash", "[coop][files]")
{
    FileSession session;
    auto& world = session.world;
    const auto sessionId = world.server().getSessionIds()[0];

    // A participant's second connection, sending data that is not what its hash says.
    test::TestPeer liar(world.network, {0.005, 0.03, 0.0, 0.0, true});
    world.server().addConnection(liar.connect(), "10.0.0.99", world.network.now());
    liar.hello(coop::AttachFiles{sessionId, session.guest().client->getParticipant(),
                                 session.guest().client->getToken()});
    world.run(0.2);
    REQUIRE(liar.link->getState() == net::Link::State::open);

    const auto claimed = randomBytes(1000, 6);
    const auto hash = net::sha256(claimed.getData(), claimed.getSize());
    liar.link->send(coop::encodeFrame(coop::FileMessage{coop::UploadQuery{hash, 1000}}));
    liar.link->send(coop::encodeFrame(coop::FileMessage{coop::UploadChunk{hash, 0, randomBytes(1000, 7)}}));
    world.run(0.2);

    std::optional<coop::UploadStatus> last;
    for (const auto& frame : liar.link->receive())
        if (const auto message = coop::decodeFileFrame(frame))
            if (const auto* status = std::get_if<coop::UploadStatus>(&*message))
                last = *status;
    REQUIRE(last.has_value());
    CHECK(last->refused);
    CHECK_FALSE(last->complete);
    CHECK(world.server().getFileBytes(sessionId) == 0);

    // Without the right token there is no files connection at all.
    test::TestPeer stranger(world.network);
    world.server().addConnection(stranger.connect(), "10.0.0.98", world.network.now());
    stranger.hello(coop::AttachFiles{sessionId, 2, "wrong"});
    world.run(0.2);
    stranger.pump();
    REQUIRE(stranger.refused.has_value());
    CHECK(stranger.refused->reason == coop::RefusalReason::badToken);
}

TEST_CASE("Files arrive whole over links that lose and reorder frames", "[coop][files]")
{
    FileSession session(server::ServerOptions{}, 8, {0.005, 0.1, 0.1, 0.05, false});
    const auto take = randomBytes(700 * 1024, 8);
    session.addTake("Audio/1/rough.wav", take);
    session.runUntilFilesArrive(300.0);
    CHECK(contentOf(session.guest().folder.getChildFile("Audio/1/rough.wav")) == take);
}
