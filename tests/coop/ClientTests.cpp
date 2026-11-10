#include "daw/coop/Client.h"
#include "daw/coop/Wire.h"
#include "daw/model/ProjectFile.h"

#include "../support/CoopWorld.h"
#include "../support/RandomProjectEdits.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw;

namespace
{

using Status = coop::Client::Status;

juce::String noticesOf(coop::Client& client)
{
    juce::StringArray texts;
    for (const auto& notice : client.takeNotices())
        texts.add(notice.text);
    return texts.joinIntoString(" ");
}

// The host's session with a project of a few tracks, and guests who joined it.
struct Session
{
    explicit Session(int guests, juce::int64 seed = 1, server::ServerOptions options = {})
        : world(seed, std::move(options))
    {
        auto& host = world.add("host");
        host.project.addTrack("Bass");
        host.project.addTrack("Drums");
        host.project.addTrack("Keys", model::TrackKind::midi);
        host.project.addBus("Reverb");
        host.client->host(test::CoopWorld::hostKey, "Ann");
        world.run(0.5);
        REQUIRE(host.client->getStatus() == Status::connected);
        REQUIRE(host.client->getInvite().has_value());

        for (int index = 0; index < guests; ++index)
        {
            auto& guest = world.add("guest" + juce::String(index));
            guest.client->join(host.client->getInvite()->code, "Guest " + juce::String(index));
        }
        world.run(1.0);
        for (auto& participant : world.participants)
            REQUIRE(participant->client->getStatus() == Status::connected);
    }

    test::CoopWorld::Participant& operator[](std::size_t index) { return *world.participants[index]; }

    // An edit as the controller makes them: a transaction, the change, sent.
    void edit(test::CoopWorld::Participant& who, const juce::String& name,
              const std::function<void(model::Project&)>& change)
    {
        who.client->setTransaction(name, who.client->newUndoStep());
        change(who.project);
        who.client->flush();
    }

    // Every copy is the server's project, exactly, in the shared form.
    void checkSame()
    {
        const auto reference = world.serverProject();
        coop::Writer expected;
        expected.writeTree(reference);
        for (auto& participant : world.participants)
        {
            const auto shared = model::sharedCopy(participant->project.getState(), participant->folder);
            coop::Writer bytes;
            bytes.writeTree(shared);
            CHECK(bytes.getData() == expected.getData());
            CHECK(model::toJson(model::Project(model::localCopy(shared, world.participants[0]->folder)),
                                world.participants[0]->folder)
                  == model::toJson(model::Project(model::localCopy(reference, world.participants[0]->folder)),
                                   world.participants[0]->folder));
        }
    }

    model::Track track(test::CoopWorld::Participant& who, int index)
    {
        return who.project.getTrack(index);
    }

    test::CoopWorld world;
};

} // namespace

TEST_CASE("A guest joins and gets the host's project", "[coop][client]")
{
    Session session(1);
    auto& host = session[0];
    auto& guest = session[1];

    CHECK(guest.project.getNumTracks() == 3);
    CHECK(guest.project.getTrack(0).getName() == "Bass");
    CHECK(guest.client->getParticipants().size() == 2);
    CHECK(host.client->isHost());
    CHECK_FALSE(guest.client->isHost());
    session.checkSame();
}

TEST_CASE("Edits reach everyone and copies stay identical", "[coop][client]")
{
    Session session(2);
    auto& host = session[0];
    auto& guest = session[1];

    session.edit(host, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.5); });
    session.edit(guest, "Rename", [](model::Project& p) { p.getTrack(1).setName("Beats"); });
    session.edit(guest, "Add Clip",
                 [&guest](model::Project& p)
                 { p.getTrack(1).addClip(guest.folder.getChildFile("Audio/2/hit.wav"), {0, 0, 1000}); });
    session.world.run(1.0);

    for (auto& participant : session.world.participants)
    {
        CHECK(participant->project.getTrack(0).getVolume() == 0.5);
        CHECK(participant->project.getTrack(1).getName() == "Beats");
        REQUIRE(participant->project.getTrack(1).getNumClips() == 1);
        // Each copy finds the file in its own folder.
        CHECK(participant->project.getTrack(1).getClip(0).getAudioFile()
              == participant->folder.getChildFile("Audio/2/hit.wav"));
    }
    CHECK(session.world.isQuiet());
    session.checkSame();

    // New ids come from each participant's own range.
    CHECK(guest.project.getTrack(1).getClip(0).getId() >= (juce::int64{2} << 40));
}

TEST_CASE("A track someone else holds cannot be changed", "[coop][client]")
{
    Session session(1);
    auto& host = session[0];
    auto& guest = session[1];
    const auto bass = model::EditScope::track(host.project.getTrack(0).getId());

    session.edit(host, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.5); });
    session.world.run(0.5);
    CHECK(guest.client->holderOf(bass) == host.client->getParticipant());
    CHECK_FALSE(guest.client->mayEdit(bass));

    // A change made anyway (a controller that did not ask) is undone at once.
    session.edit(guest, "Pan", [](model::Project& p) { p.getTrack(0).setPan(0.8); });
    CHECK(guest.project.getTrack(0).getPan() == 0.0);
    const auto notices = guest.client->takeNotices();
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].text.contains("Ann is editing track \"Bass\""));

    // After 30 s without edits it is free again.
    session.world.run(31.0, 0.05);
    CHECK(guest.client->mayEdit(bass));
    session.edit(guest, "Pan", [](model::Project& p) { p.getTrack(0).setPan(0.8); });
    session.world.run(0.5);
    CHECK(host.project.getTrack(0).getPan() == 0.8);
    session.checkSame();
}

TEST_CASE("Two people taking the same free track at once: one keeps it", "[coop][client]")
{
    Session session(1);
    auto& host = session[0];
    auto& guest = session[1];

    session.edit(host, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.3); });
    session.edit(guest, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.7); });
    session.world.run(1.0);

    // The host's arrived first: the guest's was taken back.
    CHECK(host.project.getTrack(0).getVolume() == 0.3);
    CHECK(guest.project.getTrack(0).getVolume() == 0.3);
    const auto notices = guest.client->takeNotices();
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].text.contains("was not kept"));
    CHECK_FALSE(guest.client->canUndo());
    CHECK(session.world.isQuiet());
    session.checkSame();
}

TEST_CASE("Undo is personal even among the edits of others", "[coop][client]")
{
    Session session(1);
    auto& host = session[0];
    auto& guest = session[1];

    session.edit(host, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.5); });
    session.edit(guest, "Rename", [](model::Project& p) { p.getTrack(1).setName("Beats"); });
    session.edit(host, "Add Track", [](model::Project& p) { p.addTrack("Strings"); });
    session.world.run(0.5);
    CHECK(guest.project.getNumTracks() == 4);

    CHECK(host.client->getUndoName() == "Add Track");
    REQUIRE(host.client->undo());
    session.world.run(0.5);
    CHECK(guest.project.getNumTracks() == 3);
    CHECK(host.project.getTrack(1).getName() == "Beats"); // the guest's edit stays

    REQUIRE(host.client->undo());
    session.world.run(0.5);
    CHECK(guest.project.getTrack(0).getVolume() == 1.0);
    CHECK_FALSE(host.client->canUndo());

    REQUIRE(host.client->redo());
    session.world.run(0.5);
    CHECK(guest.project.getTrack(0).getVolume() == 0.5);

    // The guest's undo touches only the guest's edit.
    REQUIRE(guest.client->undo());
    session.world.run(0.5);
    CHECK(host.project.getTrack(1).getName() == "Drums");
    CHECK(host.project.getTrack(0).getVolume() == 0.5);
    session.checkSame();
}

TEST_CASE("Undoing what someone changed since is refused", "[coop][client]")
{
    Session session(1);
    auto& host = session[0];
    auto& guest = session[1];

    session.edit(host, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.5); });
    session.world.run(31.0, 0.05); // the host's lock goes
    session.edit(guest, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.9); });
    session.world.run(0.5);

    // The guest holds the track now.
    CHECK_FALSE(host.client->undo());
    CHECK(noticesOf(*host.client).contains("is editing"));
    CHECK(host.client->canUndo());

    session.world.run(31.0, 0.05);
    CHECK_FALSE(host.client->undo());
    CHECK(noticesOf(*host.client).contains("changed it since"));
    CHECK_FALSE(host.client->canUndo());
    CHECK(host.project.getTrack(0).getVolume() == 0.9);
    session.checkSame();
}

TEST_CASE("Someone who drops out comes back without losing or repeating anything",
          "[coop][client]")
{
    Session session(1);
    auto& host = session[0];
    auto& guest = session[1];

    session.edit(guest, "Rename", [](model::Project& p) { p.getTrack(1).setName("Beats"); });
    session.world.run(0.3);

    // The connection drops with the guest's next edits in flight; the host goes on.
    session.edit(guest, "Volume", [](model::Project& p) { p.getTrack(1).setVolume(0.4); });
    session.world.network.cutAll();
    session.edit(guest, "Pan", [](model::Project& p) { p.getTrack(1).setPan(-0.5); });
    session.edit(host, "Tempo", [](model::Project& p) { p.setTempo(97.0); });
    session.world.tick();
    CHECK(guest.client->getStatus() == Status::reconnecting);
    // Still its own track: allowed while reconnecting.
    CHECK(guest.client->mayEdit(model::EditScope::track(guest.project.getTrack(1).getId())));
    CHECK_FALSE(guest.client->mayEdit(model::EditScope::track(guest.project.getTrack(2).getId())));

    session.world.run(3.0);
    CHECK(guest.client->getStatus() == Status::connected);
    CHECK(session.world.isQuiet());
    CHECK(host.project.getTrack(1).getVolume() == 0.4);
    CHECK(host.project.getTrack(1).getPan() == -0.5);
    CHECK(guest.project.getTempoMap().getTempo() == 97.0);
    CHECK(session.world.serverSequence() == 4);
    session.checkSame();
}

TEST_CASE("Everyone comes back after the server restarts", "[coop][client]")
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("daw-restart-" + juce::String(juce::Random().nextInt()));
    server::ServerOptions options;
    options.dataFolder = folder;
    Session session(2, 7, options);
    auto& host = session[0];
    auto& guest = session[1];

    session.edit(host, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.5); });
    session.edit(guest, "Rename", [](model::Project& p) { p.getTrack(1).setName("Beats"); });
    session.world.run(0.5);
    REQUIRE(session.world.isQuiet());

    // Edits made while it is down wait, and go once it is back.
    session.world.restartServer();
    session.edit(guest, "Pan", [](model::Project& p) { p.getTrack(1).setPan(0.25); });
    session.world.run(5.0);

    CHECK(session.world.isQuiet());
    CHECK(host.project.getTrack(1).getPan() == 0.25);
    CHECK(session.world.serverSequence() == 3);
    session.checkSame();
    folder.deleteRecursively();
}

TEST_CASE("A copy that stays offline becomes read-only", "[coop][client]")
{
    Session session(1);
    auto& guest = session[1];
    const auto bass = model::EditScope::track(guest.project.getTrack(0).getId());
    session.edit(guest, "Volume", [](model::Project& p) { p.getTrack(0).setVolume(0.4); });
    session.world.run(0.3);

    // The server can no longer be reached.
    session.world.conditions.loss = 1.0;
    session.world.network.cutAll();
    session.world.run(5.0, 0.05);
    CHECK(guest.client->mayEdit(bass));
    session.world.run(30.0, 0.05);
    CHECK_FALSE(guest.client->mayEdit(bass));

    session.world.conditions.loss = 0.0;
    session.world.run(15.0, 0.05);
    CHECK(guest.client->getStatus() == Status::connected);
    CHECK(session.world.isQuiet());
}

TEST_CASE("Removed participants and closed sessions end the client", "[coop][client]")
{
    Session session(2);
    auto& host = session[0];
    auto& guest = session[1];

    host.client->sendHostCommand({coop::HostCommand::Kind::remove, guest.client->getParticipant()});
    session.world.run(2.0);
    CHECK(guest.client->getStatus() == Status::ended);
    CHECK(guest.client->getProblem().contains("removed you"));
    CHECK(host.client->getParticipants().size() == 2);

    host.client->sendHostCommand({coop::HostCommand::Kind::close, 0});
    session.world.run(2.0);
    CHECK(session[2].client->getStatus() == Status::ended);
    CHECK(session[2].client->getProblem().contains("closed"));
}

TEST_CASE("Play for everyone starts every computer at the same moment", "[coop][client]")
{
    Session session(2);
    session[0].clockOffset = 0.0;
    session[1].clockOffset = 1234.5;  // clocks far apart
    session[2].clockOffset = -321.25;
    session.world.conditions = {0.01, 0.08, 0.0, 0.0}; // a slow, uneven network
    session.world.network.cutAll();
    session.world.run(5.0);

    session[1].client->requestTransport(true, 96000);
    session.world.run(1.0);

    std::vector<double> networkStarts;
    for (auto& participant : session.world.participants)
    {
        const auto starts = participant->client->takeTransportStarts();
        REQUIRE(starts.size() == 1);
        CHECK(starts[0].play);
        CHECK(starts[0].position == 96000);
        CHECK(starts[0].by == session[1].client->getParticipant());
        const auto local = participant->client->localTime(starts[0].startTime);
        REQUIRE(local.has_value());
        // The same moment for everyone, each on their own clock, within what the round
        // trips allow.
        networkStarts.push_back(*local - participant->clockOffset);
        CHECK(std::abs(networkStarts.back() - starts[0].startTime)
              <= *participant->client->clockUncertainty() + 1e-6);
    }
    CHECK(std::abs(networkStarts[1] - networkStarts[2]) < 0.1);
}

TEST_CASE("The server's clock is estimated within the round trip", "[coop][client]")
{
    Session session(1);
    auto& guest = session[1];
    session.world.run(3.0);
    const auto estimate = guest.client->serverTime(session.world.network.now());
    REQUIRE(estimate.has_value());
    // Same clock in the simulation: the estimate is off by at most half the round trip.
    CHECK(std::abs(*estimate - session.world.network.now()) <= *guest.client->clockUncertainty() + 1e-9);
    CHECK(*guest.client->clockUncertainty() < 0.05);
}

TEST_CASE("Random edits of many participants over a bad network converge byte for byte",
          "[coop][client][convergence]")
{
    struct Run
    {
        int guests;
        int edits;
        test::SimulatedNetwork::Conditions conditions;
    };
    const std::vector<Run> runs{
        {1, 2500, {0.005, 0.05, 0.0, 0.0}},
        {2, 3000, {0.01, 0.2, 0.05, 0.05}},
        {4, 4000, {0.01, 0.4, 0.15, 0.1}},
    };

    juce::int64 seed = 100;
    for (const auto& run : runs)
    {
        // Locks that go quickly: more of them change hands, and more edits race.
        server::ServerOptions options;
        options.lockIdleTimeout = 0.5;
        Session session(run.guests, ++seed, options);
        session.world.conditions = run.conditions;
        juce::Random random(seed);

        std::vector<std::unique_ptr<test::RandomProjectEditor>> editors;
        for (auto& participant : session.world.participants)
        {
            auto editor = std::make_unique<test::RandomProjectEditor>(random.nextInt64(),
                                                                       participant->folder);
            auto* client = participant->client.get();
            editor->mayEdit = [client](const model::EditScope& scope) { return client->mayEdit(scope); };
            editors.push_back(std::move(editor));
        }

        int accepted = 0;
        for (int edit = 0; edit < run.edits; ++edit)
        {
            const auto who = static_cast<std::size_t>(random.nextInt(static_cast<int>(editors.size())));
            auto& participant = session[who];
            auto& client = *participant.client;
            const int choice = random.nextInt(100);
            if (choice < 6)
                client.undo();
            else if (choice < 9)
                client.redo();
            else if (choice < 12)
            {
                coop::Presence presence;
                presence.cursorPosition = random.nextInt(100000);
                client.setPresence(presence);
            }
            else
            {
                const auto step = client.newUndoStep();
                client.setTransaction("Edit", step);
                if (editors[who]->edit(participant.project, nullptr) != "nothing")
                    ++accepted;
                client.flush();
            }

            if (random.nextBool())
                session.world.tick(random.nextDouble() * 0.03);

            // Every copy stays well formed at every moment, not only at the end.
            if (edit % 10 == 0)
                for (auto& each : session.world.participants)
                {
                    const auto check = model::checkProject(
                        model::sharedCopy(each->project.getState(), each->folder));
                    INFO("edit " << edit << ": " << check.getErrorMessage());
                    REQUIRE(check.wasOk());
                }
            if (random.nextInt(600) == 0)
                session.world.network.cutAll(); // everyone drops out and comes back
        }

        // A good network for everyone to catch up.
        session.world.conditions = {};
        session.world.network.cutAll();
        for (int wait = 0; wait < 600 && !session.world.isQuiet(); ++wait)
            session.world.run(0.1);

        INFO("guests " << run.guests << ", edits made " << accepted);
        REQUIRE(session.world.isQuiet());
        CHECK(session.world.serverSequence() > 500);
        session.checkSame();
    }
}
