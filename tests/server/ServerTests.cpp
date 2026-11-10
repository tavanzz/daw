#include "daw/server/Server.h"

#include "daw/model/ChangeRecorder.h"
#include "daw/model/Project.h"
#include "daw/model/SharedProject.h"

#include "../support/RandomProjectEdits.h"
#include "../support/TestPeer.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

using namespace daw;

namespace
{

const juce::String hostKey = "host-key-for-tests";
const juce::File copyFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("daw-server-tests-copy");

server::ServerOptions testOptions()
{
    server::ServerOptions options;
    options.hostKeys.add(hostKey);
    if (juce::SystemStats::getEnvironmentVariable("DAW_SERVER_LOG", {}).isNotEmpty())
        options.log = [](const juce::String& line) { std::cout << "server: " << line << std::endl; };
    return options;
}

struct World
{
    explicit World(server::ServerOptions options = testOptions())
        : server(std::move(options))
    {
    }

    void connect(test::TestPeer& peer, const juce::String& address = "10.0.0.1")
    {
        server.addConnection(peer.connect(), address, network.now());
    }

    // Long waits move in larger steps (what they test is the clock, not the traffic).
    void run(std::initializer_list<test::TestPeer*> peers, double seconds = 0.5)
    {
        const auto step = std::max(0.01, seconds / 2000.0);
        for (double time = 0.0; time < seconds; time += step)
        {
            for (auto* peer : peers)
                peer->pump();
            server.update(network.now());
            network.advance(step);
        }
        for (auto* peer : peers)
            peer->pump();
    }

    test::SimulatedNetwork network{1};
    server::Server server;
};

// A host with a session whose project has one track.
struct Hosted
{
    explicit Hosted(World& worldToUse)
        : world(worldToUse), host(world.network)
    {
        project.addTrack("Bass");
        world.connect(host);
        host.hello(coop::CreateSession{hostKey, "Ann"});
        world.run({&host});
        REQUIRE(host.welcome.has_value());
        host.send(coop::ProjectUpload{model::sharedCopy(project.getState(), copyFolder), {}});
        world.run({&host});
        invite = host.last<coop::Invite>().value_or(coop::Invite{}).code;
        REQUIRE(invite.isNotEmpty());
    }

    test::TestPeer& join(test::TestPeer& peer, const juce::String& name = "Marco")
    {
        world.connect(peer);
        peer.hello(coop::JoinSession{invite, name});
        world.run({&host, &peer});
        return peer;
    }

    // The changes of an edit, in the shared form, made on a copy of the session's project.
    model::ChangeSet edit(const std::function<void(model::Project&)>& change)
    {
        model::Project copy(model::localCopy(world.server.getProject(host.sessionId), copyFolder));
        model::ChangeRecorder recorder(copy.getState());
        change(copy);
        return model::sharedChanges(recorder.takeChanges(), copyFolder);
    }

    World& world;
    test::TestPeer host;
    model::Project project;
    juce::String invite;
};

// The last message of a type a peer received; there must be one.
template <typename Type>
Type lastOf(test::TestPeer& peer)
{
    auto found = peer.last<Type>();
    REQUIRE(found.has_value());
    return *found;
}

model::ObjectId firstTrack(const juce::ValueTree& project)
{
    return model::Project(project.createCopy()).getTrack(0).getId();
}

} // namespace

TEST_CASE("A host opens a session and others join it with the invite", "[server][coop]")
{
    World world;
    Hosted session(world);
    CHECK(world.server.getNumSessions() == 1);
    CHECK(session.host.number == 1);
    CHECK(session.invite.length() == 11);

    test::TestPeer guest(world.network);
    session.join(guest);
    REQUIRE(guest.welcome.has_value());
    CHECK(guest.number == 2);
    CHECK(guest.token.length() == 32);

    const auto snapshot = guest.last<coop::ProjectSnapshot>();
    REQUIRE(snapshot.has_value());
    CHECK(model::isSameTree(snapshot->project, model::sharedCopy(session.project.getState(), copyFolder)));

    const auto participants = session.host.last<coop::ParticipantList>();
    REQUIRE(participants.has_value());
    REQUIRE(participants->participants.size() == 2);
    CHECK(participants->participants[0].host);
    CHECK(participants->participants[1].name == "Marco");
    CHECK(participants->participants[1].colour != participants->participants[0].colour);

    // Codes are typed loosely.
    test::TestPeer loose(world.network);
    world.connect(loose);
    loose.hello(coop::JoinSession{session.invite.toLowerCase().replace("-", " "), "Lu"});
    world.run({&session.host, &loose});
    CHECK(loose.welcome.has_value());
}

TEST_CASE("Connections without a valid key code or token are refused", "[server][coop]")
{
    World world;

    test::TestPeer stranger(world.network);
    world.connect(stranger);
    stranger.hello(coop::CreateSession{"wrong key", "X"});
    world.run({&stranger});
    REQUIRE(stranger.refused.has_value());
    CHECK(stranger.refused->reason == coop::RefusalReason::badHostKey);
    CHECK(world.server.getNumSessions() == 0);

    Hosted session(world);

    test::TestPeer guesser(world.network);
    world.connect(guesser);
    guesser.hello(coop::JoinSession{"AAAAA-AAAAA", "X"});
    world.run({&guesser});
    REQUIRE(guesser.refused.has_value());
    CHECK(guesser.refused->reason == coop::RefusalReason::badInvite);

    test::TestPeer impostor(world.network);
    world.connect(impostor);
    impostor.hello(coop::RejoinSession{session.host.sessionId, 1, "not the token"});
    world.run({&impostor});
    REQUIRE(impostor.refused.has_value());
    CHECK(impostor.refused->reason == coop::RefusalReason::badToken);

    test::TestPeer silent(world.network);
    world.connect(silent);
    world.run({&silent}, 11.0);
    CHECK(silent.link->getState() == net::Link::State::closed);
}

TEST_CASE("Too many wrong codes from one address block it for a while", "[server][coop]")
{
    World world;
    Hosted session(world);

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        test::TestPeer guesser(world.network);
        world.connect(guesser, "203.0.113.9");
        guesser.hello(coop::JoinSession{"AAAAA-AAAA" + juce::String(attempt), "X"});
        world.run({&guesser});
    }

    test::TestPeer late(world.network);
    world.connect(late, "203.0.113.9");
    late.hello(coop::JoinSession{session.invite, "X"});
    world.run({&late});
    REQUIRE(late.refused.has_value());
    CHECK(late.refused->reason == coop::RefusalReason::tooManyAttempts);
    CHECK(coop::describe(*late.refused).contains("15 minutes"));

    // Another address is not affected, and the block ends.
    test::TestPeer other(world.network);
    session.join(other);
    CHECK(other.welcome.has_value());

    world.run({&session.host}, 901.0);
    test::TestPeer later(world.network);
    world.connect(later, "203.0.113.9");
    later.hello(coop::JoinSession{session.invite, "X"});
    world.run({&session.host, &later});
    CHECK(later.welcome.has_value());
}

TEST_CASE("Expired renewed and revoked invites", "[server][coop]")
{
    World world;
    Hosted session(world);
    const auto first = session.invite;

    world.run({&session.host}, 24.0 * 3600.0 + 1.0);
    test::TestPeer late(world.network);
    world.connect(late);
    late.hello(coop::JoinSession{first, "X"});
    world.run({&session.host, &late});
    REQUIRE(late.refused.has_value());
    CHECK(late.refused->reason == coop::RefusalReason::expiredInvite);

    session.host.send(coop::HostCommand{coop::HostCommand::Kind::newInvite, 0});
    world.run({&session.host});
    const auto renewed = session.host.last<coop::Invite>();
    REQUIRE(renewed.has_value());
    CHECK(renewed->code != first);

    session.host.send(coop::HostCommand{coop::HostCommand::Kind::revokeInvite, 0});
    world.run({&session.host});
    test::TestPeer refused(world.network);
    world.connect(refused);
    refused.hello(coop::JoinSession{renewed->code, "X"});
    world.run({&session.host, &refused});
    REQUIRE(refused.refused.has_value());
    CHECK(refused.refused->reason == coop::RefusalReason::badInvite);
}

TEST_CASE("Peers of another protocol or project format are refused clearly", "[server][coop]")
{
    World world;

    test::TestPeer newer(world.network);
    world.connect(newer);
    newer.hello(coop::CreateSession{hostKey, "X"},
                static_cast<std::uint32_t>(model::networkProtocolVersion + 1));
    world.run({&newer});
    REQUIRE(newer.refused.has_value());
    CHECK(newer.refused->reason == coop::RefusalReason::incompatibleProtocol);
    CHECK(newer.refused->serverProtocol == static_cast<std::uint32_t>(model::networkProtocolVersion));
    CHECK(coop::describe(*newer.refused).contains("older version"));

    test::TestPeer otherFormat(world.network);
    world.connect(otherFormat);
    otherFormat.hello(coop::CreateSession{hostKey, "X"},
                      static_cast<std::uint32_t>(model::networkProtocolVersion),
                      static_cast<std::uint32_t>(model::currentFormatVersion - 1));
    world.run({&otherFormat});
    REQUIRE(otherFormat.refused.has_value());
    CHECK(otherFormat.refused->reason == coop::RefusalReason::incompatibleProjectFormat);
    CHECK(world.server.getNumSessions() == 0);
}

TEST_CASE("Transactions are ordered applied and passed to everyone", "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network);
    session.join(guest);
    guest.take<coop::AcceptedChange>();

    const auto changes = session.edit([](model::Project& project) { project.addTrack("Drums"); });
    session.host.send(coop::Transaction{1, "Add Track", 1, changes, {}});
    world.run({&session.host, &guest});

    for (auto* peer : {&session.host, &guest})
    {
        const auto accepted = peer->take<coop::AcceptedChange>();
        REQUIRE(accepted.size() == 1);
        CHECK(accepted[0].sequence == 1);
        CHECK(accepted[0].author == 1);
        CHECK(accepted[0].clientSequence == 1);
    }
    CHECK(world.server.getSequence(session.host.sessionId) == 1);
    CHECK(model::Project(world.server.getProject(session.host.sessionId).createCopy()).getNumTracks()
          == 2);

    // The host holds the project's lock and the new track's.
    const auto locks = world.server.getLocks(session.host.sessionId);
    REQUIRE(locks.size() == 2);
    CHECK(locks[0].first == model::EditScope::project());
    CHECK(locks[0].second == 1);
    CHECK(locks[1].first.kind == model::EditScope::Kind::track);

    // A transaction sent twice counts once.
    session.host.send(coop::Transaction{1, "Add Track", 1, changes, {}});
    world.run({&session.host, &guest});
    CHECK(world.server.getSequence(session.host.sessionId) == 1);
}

TEST_CASE("Nobody changes a track someone else holds until the lock goes", "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network);
    session.join(guest);
    const auto track = firstTrack(world.server.getProject(session.host.sessionId));

    session.host.send(coop::Transaction{
        1, "Volume", 1, session.edit([](model::Project& p) { p.getTrack(0).setVolume(0.5); }), {}});
    world.run({&session.host, &guest});
    const auto lockNews = guest.take<coop::LockState>();
    REQUIRE_FALSE(lockNews.empty());
    CHECK(lockNews.back().locks[0] == std::make_pair(model::EditScope::track(track), 1u));

    guest.send(coop::Transaction{
        1, "Pan", 1, session.edit([](model::Project& p) { p.getTrack(0).setPan(0.3); }), {}});
    world.run({&session.host, &guest});
    auto rejection = guest.last<coop::Rejection>();
    REQUIRE(rejection.has_value());
    CHECK(rejection->reason == coop::RejectionReason::locked);
    CHECK(rejection->holder == 1);
    CHECK(model::Project(world.server.getProject(session.host.sessionId).createCopy())
              .getTrack(0)
              .getPan()
          == 0.0);

    // Asking for it says who has it.
    guest.send(coop::LockRequest{model::EditScope::track(track), true});
    world.run({&session.host, &guest});
    CHECK(lastOf<coop::LockState>(guest).locks[0].second == 1);

    // 30 s without edits: free for the next one.
    world.run({&session.host, &guest}, 31.0);
    CHECK(lastOf<coop::LockState>(guest).locks[0].second == 0);
    guest.send(coop::Transaction{
        2, "Pan", 2, session.edit([](model::Project& p) { p.getTrack(0).setPan(0.3); }), {}});
    world.run({&session.host, &guest});
    CHECK(guest.take<coop::Rejection>().empty());
    CHECK(model::Project(world.server.getProject(session.host.sessionId).createCopy())
              .getTrack(0)
              .getPan()
          == 0.3);
}

TEST_CASE("Locks of someone who dropped out wait for them then go", "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network);
    session.join(guest);
    const auto track = firstTrack(world.server.getProject(session.host.sessionId));

    guest.send(coop::LockRequest{model::EditScope::track(track), true});
    world.run({&session.host, &guest});
    CHECK(world.server.getLocks(session.host.sessionId).size() == 1);

    guest.dropConnection();
    world.run({&session.host}, 20.0);
    CHECK(world.server.getLocks(session.host.sessionId).size() == 1);
    const auto list = session.host.last<coop::ParticipantList>();
    REQUIRE(list.has_value());
    CHECK_FALSE(list->participants[1].connected);

    world.run({&session.host}, 11.0);
    CHECK(world.server.getLocks(session.host.sessionId).empty());
}

TEST_CASE("Transactions that do not fit are refused and change nothing", "[server][coop]")
{
    World world;
    Hosted session(world);
    const auto before = world.server.getProject(session.host.sessionId).createCopy();

    // Made on a project that is not the session's.
    model::Project other;
    other.addTrack("Elsewhere").setVolume(0.5);
    model::ChangeRecorder recorder(other.getState());
    other.getTrack(0).setVolume(0.2);
    session.host.send(coop::Transaction{1, "x", 1, model::sharedChanges(recorder.takeChanges(), copyFolder), {}});
    world.run({&session.host});
    auto rejection = session.host.last<coop::Rejection>();
    REQUIRE(rejection.has_value());
    CHECK(rejection->reason == coop::RejectionReason::conflict);

    // A clip in the project's root.
    model::ProjectChange misplaced;
    misplaced.kind = model::ProjectChange::Kind::insertChild;
    juce::ValueTree clip("Clip");
    clip.setProperty("id", juce::int64{99}, nullptr);
    misplaced.child = clip;
    session.host.send(coop::Transaction{2, "x", 2, {misplaced}, {}});
    world.run({&session.host});
    rejection = session.host.last<coop::Rejection>();
    REQUIRE(rejection.has_value());
    CHECK(rejection->reason == coop::RejectionReason::malformed);

    // A clip naming a file outside the copy, after a valid change in the same transaction.
    auto changes = session.edit(
        [](model::Project& p)
        {
            p.getTrack(0).setVolume(0.7);
            p.getTrack(0).addClip(copyFolder.getChildFile("Audio/1/a.wav"), {0, 0, 10});
        });
    changes.back().child.setProperty("audioFile", "../../secret.wav", nullptr);
    session.host.send(coop::Transaction{3, "x", 3, changes, {}});
    world.run({&session.host});
    rejection = session.host.last<coop::Rejection>();
    REQUIRE(rejection.has_value());
    CHECK(rejection->reason == coop::RejectionReason::malformed);

    CHECK(test::isExactlyTheSame(world.server.getProject(session.host.sessionId), before));
    CHECK(world.server.getSequence(session.host.sessionId) == 0);
}

TEST_CASE("Someone back after a drop gets what they missed once", "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network);
    session.join(guest);
    guest.take<coop::AcceptedChange>();

    guest.dropConnection();
    for (std::uint64_t index = 1; index <= 5; ++index)
    {
        // Each one made on what the server has after the one before.
        session.host.send(coop::Transaction{
            index, "Volume", index,
            session.edit([index](model::Project& p)
                         { p.getTrack(0).setVolume(0.1 * static_cast<double>(index)); }),
            {}});
        world.run({&session.host});
    }
    CHECK(world.server.getSequence(session.host.sessionId) == 5);

    world.connect(guest);
    guest.rejoin();
    world.run({&session.host, &guest});
    REQUIRE(guest.welcome.has_value());
    CHECK(guest.welcome->resumed);
    const auto missed = guest.take<coop::AcceptedChange>();
    CHECK(missed.size() == world.server.getSequence(session.host.sessionId));
    for (std::size_t index = 0; index < missed.size(); ++index)
        CHECK(missed[index].sequence == index + 1);
}

TEST_CASE("The host removes participants hands over and closes the session", "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network), third(world.network);
    session.join(guest);
    session.join(third, "Lu");

    session.host.send(coop::HostCommand{coop::HostCommand::Kind::remove, 3});
    world.run({&session.host, &guest, &third}, 2.0);
    CHECK(lastOf<coop::SessionEnded>(third).reason == coop::SessionEnded::Reason::removed);
    world.connect(third);
    third.rejoin();
    world.run({&session.host, &guest, &third});
    REQUIRE(third.refused.has_value());
    CHECK(third.refused->reason == coop::RefusalReason::removed);

    // Only the host may.
    guest.send(coop::HostCommand{coop::HostCommand::Kind::close, 0});
    world.run({&session.host, &guest});
    CHECK(world.server.getNumSessions() == 1);

    session.host.send(coop::HostCommand{coop::HostCommand::Kind::handOver, 2});
    world.run({&session.host, &guest});
    CHECK(guest.last<coop::Invite>().has_value());
    const auto list = guest.last<coop::ParticipantList>();
    REQUIRE(list.has_value());
    CHECK_FALSE(list->participants[0].host);
    CHECK(list->participants[1].host);

    const auto sessionId = session.host.sessionId;
    guest.send(coop::HostCommand{coop::HostCommand::Kind::close, 0});
    world.run({&session.host, &guest}, 2.0);
    CHECK(lastOf<coop::SessionEnded>(session.host).reason == coop::SessionEnded::Reason::closed);
    CHECK(world.server.getNumSessions() == 0);

    world.connect(session.host);
    session.host.hello(coop::RejoinSession{sessionId, 1, session.host.token});
    world.run({&session.host});
    REQUIRE(session.host.refused.has_value());
    CHECK(session.host.refused->reason == coop::RefusalReason::sessionClosed);
}

TEST_CASE("While someone records the tempo and effect chains stay as they are", "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network);
    session.join(guest);

    guest.send(coop::RecordingState{true});
    world.run({&session.host, &guest});
    CHECK(lastOf<coop::ParticipantList>(session.host).participants[1].recording);

    session.host.send(coop::Transaction{
        1, "Tempo", 1, session.edit([](model::Project& p) { p.setTempo(90.0); }), {}});
    session.host.send(coop::Transaction{
        2, "Add Effect", 2,
        session.edit([](model::Project& p) { p.getTrack(0).getEffects().add("equaliser", 0); }), {}});
    world.run({&session.host, &guest});
    const auto rejections = session.host.take<coop::Rejection>();
    REQUIRE(rejections.size() == 2);
    CHECK(rejections[0].reason == coop::RejectionReason::recording);
    CHECK(rejections[0].holder == 2);

    session.host.send(coop::Transaction{
        3, "Rename", 3, session.edit([](model::Project& p) { p.getTrack(0).setName("Bass 2"); }), {}});
    world.run({&session.host, &guest});
    CHECK(session.host.take<coop::Rejection>().empty());
}

TEST_CASE("Presence is passed on at most twenty times a second and clocks are answered",
          "[server][coop]")
{
    World world;
    Hosted session(world);
    test::TestPeer guest(world.network);
    session.join(guest);
    guest.take<coop::PresenceUpdate>();

    for (int index = 0; index < 100; ++index)
    {
        coop::Presence presence;
        presence.cursorPosition = index;
        session.host.send(coop::PresenceUpdate{0, presence});
        world.run({&session.host, &guest}, 0.01);
    }
    world.run({&session.host, &guest});
    const auto updates = guest.take<coop::PresenceUpdate>();
    CHECK(updates.size() <= 25);
    REQUIRE_FALSE(updates.empty());
    CHECK(updates.back().participant == 1);
    CHECK(updates.back().presence.cursorPosition == 99);
    CHECK(session.host.take<coop::PresenceUpdate>().empty());

    guest.send(coop::Ping{12.5});
    world.run({&session.host, &guest});
    const auto pong = guest.last<coop::Pong>();
    REQUIRE(pong.has_value());
    CHECK(pong->clientTime == 12.5);
    CHECK(pong->serverTime > 0.0);

    session.host.send(coop::TransportRequest{true, 1000});
    world.run({&session.host, &guest});
    const auto start = guest.last<coop::TransportStart>();
    REQUIRE(start.has_value());
    CHECK(start->by == 1);
    CHECK(start->position == 1000);
}

TEST_CASE("Sessions survive a server that stops and starts again", "[server][coop]")
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("daw-server-store-" + juce::String(juce::Random().nextInt()));
    auto options = testOptions();
    options.dataFolder = folder;

    const auto crashed = folder.getSiblingFile(folder.getFileName() + "-crashed");
    juce::String sessionId, token;
    juce::ValueTree project;
    {
        World world(options);
        Hosted session(world);
        for (std::uint64_t index = 1; index <= 3; ++index)
        {
            session.host.send(coop::Transaction{
                index, "Track", index,
                session.edit([](model::Project& p) { p.addTrack("New"); }), {}});
            world.run({&session.host});
        }
        sessionId = session.host.sessionId;
        token = session.host.token;
        project = world.server.getProject(sessionId).createCopy();
        CHECK(world.server.getSequence(sessionId) == 3);

        // What a crash now would leave: the last changes only in the journal.
        CHECK(folder.getChildFile("sessions").getChildFile(sessionId).getChildFile("journal.bin")
                  .existsAsFile());
        REQUIRE(folder.copyDirectoryTo(crashed));
    }

    for (const auto& start : {folder, crashed})
    {
    options.dataFolder = start;
    World world(options);
    CHECK(world.server.getNumSessions() == 1);
    CHECK(world.server.getSequence(sessionId) == 3);
    CHECK(test::isExactlyTheSame(world.server.getProject(sessionId), project));

    test::TestPeer host(world.network);
    world.connect(host);
    host.sessionId = sessionId;
    host.number = 1;
    host.token = token;
    host.rejoin();
    world.run({&host});
    REQUIRE(host.welcome.has_value());
    CHECK_FALSE(host.welcome->resumed);
    CHECK(host.welcome->lastTransaction == 3);
    const auto snapshot = host.last<coop::ProjectSnapshot>();
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->sequence == 3);

    // Tokens are kept hashed.
    const auto stored = start.getChildFile("sessions").getChildFile(sessionId).getChildFile("session.json");
    CHECK_FALSE(stored.loadFileAsString().contains(token));
    }
    folder.deleteRecursively();
    crashed.deleteRecursively();
}
