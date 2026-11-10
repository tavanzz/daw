#include "daw/controller/CoopSession.h"
#include "daw/controller/Session.h"
#include "daw/model/SharedProject.h"
#include "daw/server/Server.h"

#include "daw/controller/PluginScanner.h"

#include "support/RealtimeCheck.h"
#include "support/SimulatedDevice.h"
#include "support/SimulatedNetwork.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"
#include "support/TestPlugins.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace daw;
using daw::test::AudioFileType;
using daw::test::TemporaryDirectory;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
const juce::String hostKey = "controller test key";

juce::File writeTone(const juce::File& file, int numSamples, float value)
{
    juce::AudioBuffer<float> samples(2, numSamples);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), value, numSamples);
    file.getParentDirectory().createDirectory();
    REQUIRE(daw::test::writeAudioFile(file, AudioFileType::wav, samples, sampleRate, 32, true));
    return file;
}

// The VST3 test plugins, scanned.
controller::PluginCatalogue testCatalogue()
{
    controller::PluginCatalogue catalogue;
    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);
    const controller::ScanItem item{"VST3", test::testVst3("Gain").getFullPathName()};
    auto outcome = controller::scanItem(formats, item, {});
    REQUIRE_FALSE(outcome.failure);
    catalogue.addScanned(item, std::move(outcome.plugins));
    return catalogue;
}

// What a computer has: a sound card (for recording), plugins.
struct Equipment
{
    bool device = false;
    bool plugins = false;          ///< hosts plugins...
    bool testPluginsInstalled = true; ///< ...and has the test plugins
};

// A computer: an engine as the device runs it, a session, its part in the co-op session.
struct Computer
{
    Computer(test::SimulatedNetwork& network, server::Server& server, Equipment equipment = {})
        : catalogue(equipment.plugins && equipment.testPluginsInstalled ? testCatalogue()
                                                                        : controller::PluginCatalogue{}),
          session(engine, sessionOptions(equipment))
    {
        if (equipment.device)
        {
            device = std::make_unique<test::SimulatedDevice>(engine, sampleRate, 2,
                                                             std::vector<int>{blockSize});
            device->inputSignal = test::testInputSample;
            device->start();
        }
        else
            engine.prepare(sampleRate, blockSize);
        session.setSampleRate(sampleRate);
        controller::CoopSession::Options options;
        options.appVersion = "test";
        options.clock = [&network] { return network.now(); };
        coop = std::make_unique<controller::CoopSession>(
            session,
            [&network, &server](const controller::CoopServer&)
            {
                auto pair = network.connect({0.005, 0.03, 0.0, 0.0, true});
                server.addConnection(std::move(pair.server), "10.0.0.1", network.now());
                return std::move(pair.client);
            },
            options);
    }

    controller::SessionOptions sessionOptions(const Equipment& equipment)
    {
        controller::SessionOptions options;
        options.streaming = equipment.device ? engine::io::DiskStreamer::Mode::readAhead
                                             : engine::io::DiskStreamer::Mode::manual;
        if (equipment.plugins)
        {
            options.plugins = &loader;
            options.catalogue = &catalogue;
        }
        return options;
    }

    // One tick of the app: the co-op session, the session, an audio block.
    void tick()
    {
        coop->update();
        session.update();
        if (device != nullptr)
            heapOperations += device->run(blockSize);
        else
            heapOperations += test::heapOperationsDuring([this] { engine.process(block); });
    }

    engine::Engine engine;
    controller::PluginLoader loader{controller::PluginLoader::formatsWithoutEditors()};
    controller::PluginCatalogue catalogue;
    controller::Session session;
    std::unique_ptr<test::SimulatedDevice> device;
    std::unique_ptr<controller::CoopSession> coop;
    juce::AudioBuffer<float> block{2, blockSize};
    std::size_t heapOperations = 0;
};

struct World
{
    World()
    {
        options.hostKeys.add(hostKey);
        server = std::make_unique<server::Server>(options);
    }

    Computer& add(Equipment equipment = {})
    {
        computers.push_back(std::make_unique<Computer>(network, *server, equipment));
        return *computers.back();
    }

    void run(double seconds)
    {
        for (double time = 0.0; time < seconds; time += 0.01)
        {
            for (auto& computer : computers)
                computer->tick();
            server->update(network.now());
            network.advance(0.01);
        }
    }

    // Until every copy has every file and nothing waits.
    void settle(double limit = 30.0)
    {
        for (double time = 0.0; time < limit; time += 0.1)
        {
            run(0.1);
            bool quiet = true;
            for (auto& computer : computers)
            {
                const auto& client = computer->coop->getClient();
                quiet = quiet && client.getNumPending() == 0 && client.getFiles().isIdle()
                        && !computer->session.hasEditsWaitingForFiles()
                        && client.getSequence() == server->getSequence(server->getSessionIds()[0]);
            }
            if (quiet)
                return;
        }
    }

    controller::CoopServerLink link() const
    {
        return {{"server.test", 7443, juce::String::repeatedString("A", 43)}, hostKey};
    }

    test::SimulatedNetwork network{11};
    server::ServerOptions options;
    std::unique_ptr<server::Server> server;
    std::vector<std::unique_ptr<Computer>> computers;
};

// A host with a project of an audio track playing a file from outside the copy, and a
// guest who joined.
struct Shared
{
    explicit Shared(Equipment hostEquipment = {}, Equipment guestEquipment = {})
        : host(world.add(hostEquipment)), guest(world.add(guestEquipment))
    {
        original = writeTone(elsewhere.file("Samples/tone.wav"), 48000, 0.25f);
        const auto ids = host.session.importAudio({original}, 0);
        REQUIRE(ids.size() == 1);
        REQUIRE(host.coop->host(world.link(), "Ann", hostFolder.file("Copy")).wasOk());
        world.run(1.0);
        REQUIRE(host.coop->getClient().getStatus() == coop::Client::Status::connected);

        const auto invite = controller::parseInvite(host.coop->getInviteLink());
        REQUIRE(invite.has_value());
        REQUIRE(guest.coop->join(*invite, "Marco", guestFolder.file("Copy")).wasOk());
        world.settle();
        REQUIRE(guest.coop->getClient().getStatus() == coop::Client::Status::connected);
    }

    TemporaryDirectory elsewhere, hostFolder, guestFolder;
    World world;
    Computer& host;
    Computer& guest;
    juce::File original;
};

model::ObjectId firstTrack(const controller::Session& session)
{
    return session.getProject().getTrack(0).getId();
}

} // namespace

TEST_CASE("Invite and server links survive being pasted", "[controller][coop]")
{
    const controller::CoopServer server{"203.0.113.7", 443, juce::String::repeatedString("x", 43)};
    const controller::CoopInvite invite{server, "ABCDE-FGHJK", "Song & Dance/1"};
    const auto parsed = controller::parseInvite("  " + controller::inviteLink(invite) + "\n");
    REQUIRE(parsed.has_value());
    CHECK(parsed->server == server);
    CHECK(parsed->code == "ABCDE-FGHJK");
    CHECK(parsed->title == "Song & Dance/1");
    CHECK(controller::parseInvite(controller::inviteLink({server, "ABCDE-FGHJK", {}}))->title.isEmpty());

    const controller::CoopServerLink link{{"::1", 7443, server.fingerprint}, "a key/with+odd=chars"};
    const auto parsedLink = controller::parseServerLink(controller::serverLink(link));
    REQUIRE(parsedLink.has_value());
    CHECK(parsedLink->server.host == "::1");
    CHECK(parsedLink->server.port == 7443);
    CHECK(parsedLink->hostKey == "a key/with+odd=chars");

    CHECK_FALSE(controller::parseInvite("https://example.com/?code=x").has_value());
    CHECK_FALSE(controller::parseInvite(controller::serverLink(link)).has_value());
}

TEST_CASE("Copies of co-op projects get folders of their own, named after the project", "[controller][coop]")
{
    TemporaryDirectory temporary;
    const auto parent = temporary.file("Co-op");
    const auto first = controller::newCopyFolder(parent, "Song: Take/2");
    CHECK(first.getParentDirectory() == parent);
    CHECK_FALSE(first.getFileName().containsAnyOf(":/"));
    REQUIRE(first.createDirectory());
    const auto second = controller::newCopyFolder(parent, "Song: Take/2");
    CHECK(second.getFileName() == first.getFileName() + " 2");
    CHECK(controller::newCopyFolder(parent, "  ").getFileName() == "Session");
    CHECK(controller::newCopyFolder(parent, "..hidden").getFileName() == "hidden");
}

TEST_CASE("Hosting copies the project and its files; the guest gets both", "[controller][coop]")
{
    Shared shared;
    auto& host = shared.host;
    auto& guest = shared.guest;

    // The original is left as it was; the copy plays a copy of the file.
    CHECK(shared.original.existsAsFile());
    const auto hostClip = host.session.getProject().getTrack(0).getClip(0).getAudioFile();
    CHECK(hostClip.isAChildOf(shared.hostFolder.file("Copy/Audio/1")));
    CHECK(hostClip.getFileName() == "tone.wav");

    REQUIRE(guest.session.getProject().getNumTracks() == 1);
    const auto guestClip = guest.session.getProject().getTrack(0).getClip(0).getAudioFile();
    CHECK(guestClip == shared.guestFolder.file("Copy/Audio/1/tone.wav"));
    CHECK(guestClip.existsAsFile());
    CHECK(guestClip.getSize() == hostClip.getSize());

    // The guest's session plays it.
    REQUIRE(guest.session.waitForLoads(10000));
    const auto* source = guest.session.findSource(guestClip);
    REQUIRE(source != nullptr);
    CHECK(source->status == engine::io::PooledSource::Status::ready);

    // Both copies are the same project.
    CHECK(model::isSameTree(model::sharedCopy(host.session.getProject().getState(), shared.hostFolder.file("Copy")),
                            model::sharedCopy(guest.session.getProject().getState(),
                                              shared.guestFolder.file("Copy"))));
    // And each can come back: the membership is written.
    CHECK(controller::readMembership(shared.guestFolder.file("Copy")).has_value());
}

TEST_CASE("Edits go through the locks with personal undo", "[controller][coop]")
{
    Shared shared;
    auto& host = shared.host;
    auto& guest = shared.guest;
    const auto track = firstTrack(host.session);

    host.session.setTrackVolume(track, 0.5);
    shared.world.run(0.5);
    CHECK(guest.session.getProject().getTrack(0).getVolume() == 0.5);

    // The guest cannot change it now, and learns why.
    guest.session.setTrackPan(track, 0.4);
    CHECK(guest.session.getProject().getTrack(0).getPan() == 0.0);
    CHECK(guest.session.takeEditProblem() == "Ann is editing track \"tone\".");

    // A gesture is one undo step, however many transactions it took.
    guest.session.addBus("Reverb");
    shared.world.run(0.5);
    const auto bus = guest.session.getProject().getBus(0).getId();
    guest.session.beginGesture("Bus Volume");
    for (int step = 1; step <= 10; ++step)
    {
        guest.session.setBusVolume(bus, 1.0 - step * 0.05);
        shared.world.run(0.03);
    }
    guest.session.endGesture();
    shared.world.settle();
    CHECK(host.session.getProject().getBus(0).getVolume() == 0.5);
    CHECK(guest.session.getUndoDescription().contains("Bus Volume"));

    REQUIRE(guest.session.undo());
    shared.world.settle();
    CHECK(host.session.getProject().getBus(0).getVolume() == 1.0);
    // The host's own edit stays.
    CHECK(host.session.getProject().getTrack(0).getVolume() == 0.5);
}

TEST_CASE("An import goes into the participant's folder and reaches the others",
          "[controller][coop]")
{
    Shared shared;
    auto& host = shared.host;
    auto& guest = shared.guest;
    const auto file = writeTone(shared.elsewhere.file("Downloads/hit.wav"), 24000, 0.5f);

    const auto ids = guest.session.importAudio({file}, model::ticksPerSecond);
    CHECK(ids.empty()); // added once hashed
    CHECK(guest.session.hasEditsWaitingForFiles());

    // The host gets the clip at once and its file soon after: meanwhile views say so.
    const auto hostCopy = shared.hostFolder.file("Copy/Audio/2/hit.wav");
    juce::StringArray seen;
    for (int step = 0; step < 500 && !hostCopy.existsAsFile(); ++step)
    {
        shared.world.run(0.01);
        if (host.session.getProject().getNumTracks() == 2)
            seen.addIfNotAlreadyThere(host.coop->describeFile(hostCopy).upToFirstOccurrenceOf(" ", false, false));
    }
    INFO(seen.joinIntoString(", ").toStdString());
    CHECK((seen.contains("Incoming") || seen.contains("Downloading")));
    shared.world.settle();
    CHECK(host.coop->describeFile(hostCopy).isEmpty());
    CHECK_FALSE(host.coop->areFilesMoving());

    REQUIRE(host.session.getProject().getNumTracks() == 2);
    const auto clip = host.session.getProject().getTrack(1).getClip(0);
    CHECK(clip.getAudioFile() == shared.hostFolder.file("Copy/Audio/2/hit.wav"));
    CHECK(clip.getAudioFile().existsAsFile());
    CHECK(clip.getLength().has_value());
    CHECK(guest.session.getProject().getTrack(1).getClip(0).getAudioFile()
          == shared.guestFolder.file("Copy/Audio/2/hit.wav"));
}

TEST_CASE("Changes from the network never make the audio thread allocate", "[controller][coop]")
{
    Shared shared;
    auto& host = shared.host;
    auto& guest = shared.guest;
    const auto track = firstTrack(host.session);

    guest.session.play();
    guest.heapOperations = 0;
    juce::Random random(3);
    for (int edit = 0; edit < 200; ++edit)
    {
        switch (edit % 5)
        {
        case 0:
            host.session.setTrackVolume(track, random.nextDouble());
            break;
        case 1:
            host.session.setTrackPan(track, random.nextDouble() * 2.0 - 1.0);
            break;
        case 2:
            host.session.addEffect(controller::ChainOwner::track(track), "equaliser");
            break;
        case 3:
            host.session.setClipBounds(host.session.getProject().getTrack(0).getClip(0).getId(),
                                       {random.nextInt(1000) * 1000LL, 0, 24000LL * 5880});
            break;
        default:
            host.session.setTempo(90.0 + random.nextInt(60));
            break;
        }
        shared.world.run(0.03);
    }
    shared.world.settle();
    CHECK(guest.heapOperations == 0);
    CHECK(host.heapOperations == 0);
    CHECK(guest.session.getProject().getTrack(0).getEffects().getNumEffects() == 40);
}

TEST_CASE("A copy comes back to its session after the app starts again", "[controller][coop]")
{
    Shared shared;
    const auto guestFolder = shared.guestFolder.file("Copy");
    const auto track = firstTrack(shared.host.session);

    // The guest's app quits; meanwhile the host edits.
    REQUIRE(shared.guest.coop->saveCopy().wasOk());
    shared.world.computers.pop_back();
    shared.host.session.setTrackVolume(track, 0.3);
    shared.world.run(0.5);

    auto& again = shared.world.add();
    REQUIRE(again.coop->rejoin(guestFolder).wasOk());
    shared.world.settle();
    CHECK(again.coop->getClient().getStatus() == coop::Client::Status::connected);
    CHECK(again.session.getProject().getTrack(0).getVolume() == 0.3);
    CHECK(again.coop->getClient().getParticipant() == 2);
}

TEST_CASE("Play for Everyone starts the followers' engines on the same moment",
          "[controller][coop]")
{
    Shared shared;
    shared.guest.coop->playForEveryone(model::ticksPerSecond / 10);
    shared.world.run(0.4); // the file lasts a second
    for (auto* computer : {&shared.host, &shared.guest})
        CHECK(computer->session.getTransportState() == engine::TransportState::playing);

    shared.guest.coop->setFollowing(false);
    shared.host.coop->stopForEveryone();
    shared.world.run(0.2);
    CHECK(shared.host.session.getTransportState() == engine::TransportState::stopped);
    CHECK(shared.guest.session.getTransportState() == engine::TransportState::playing);
}

TEST_CASE("A take recorded in co-op reaches everyone once it is hashed", "[controller][coop]")
{
    Shared shared({true, false});
    auto& host = shared.host;
    auto& guest = shared.guest;
    const auto track = firstTrack(host.session);
    REQUIRE(host.session.setTrackArmed(track, true)); // personal: the guest's stays off
    shared.world.run(0.2);
    CHECK_FALSE(guest.session.getProject().getTrack(0).isArmed());

    REQUIRE(host.session.record().wasOk());
    shared.world.run(0.5);

    // While it records, its track is the host's, and the tempo stays.
    guest.session.setTrackVolume(track, 0.1);
    CHECK(guest.session.takeEditProblem().contains("Ann is editing"));
    guest.session.setTempo(100.0);
    shared.world.run(0.5);
    CHECK(guest.coop->takeNotices().joinIntoString(" ").contains("Ann is recording"));

    host.session.stop();
    for (int wait = 0; wait < 300 && (host.session.isRecording() || host.session.hasEditsWaitingForFiles()); ++wait)
    {
        shared.world.run(0.05);
        juce::Thread::sleep(1);
    }
    shared.world.settle();

    REQUIRE(guest.session.getProject().getTrack(0).getNumClips() == 2);
    const auto take = guest.session.getProject().getTrack(0).getClip(1).getAudioFile();
    CHECK(take.isAChildOf(shared.guestFolder.file("Copy/Audio/1")));
    CHECK(take.existsAsFile());
    CHECK(guest.session.getProject().getTempoMap().getTempo() == 120.0);
    CHECK(host.heapOperations == 0);
}

TEST_CASE("Presence reaches the others with the playhead", "[controller][coop]")
{
    Shared shared;
    coop::Presence presence;
    presence.track = firstTrack(shared.host.session);
    presence.hasCursor = true;
    presence.cursorPosition = 12345;
    shared.host.coop->setPresence(presence);
    shared.host.session.seek(model::ticksPerSecond / 4);
    shared.world.run(0.5);

    const auto& presences = shared.guest.coop->getClient().getPresences();
    REQUIRE(presences.count(1) == 1);
    CHECK(presences.at(1).track == presence.track);
    CHECK(presences.at(1).cursorPosition == 12345);
    CHECK(presences.at(1).playhead == model::ticksPerSecond / 4);
}

TEST_CASE("A plugin's state follows whoever holds its track; a missing one is named",
          "[controller][coop]")
{
    Shared shared({false, true}, {false, true});
    auto& host = shared.host;
    auto& guest = shared.guest;
    const auto track = firstTrack(host.session);
    const auto& plugins = host.catalogue.getPlugins();
    REQUIRE_FALSE(plugins.empty());

    const auto effectId = host.session.addPlugin(controller::ChainOwner::track(track),
                                                 plugins.front().description);
    REQUIRE(effectId != 0);
    for (int wait = 0; wait < 100 && (host.session.findPlugin(effectId) == nullptr
                                      || guest.session.findPlugin(effectId) == nullptr);
         ++wait)
        shared.world.run(0.05);
    auto* hostPlugin = host.session.findPlugin(effectId);
    auto* guestPlugin = guest.session.findPlugin(effectId);
    REQUIRE(hostPlugin != nullptr);
    REQUIRE(guestPlugin != nullptr);

    // The host's plugin holds another state; whoever holds the track sends it.
    hostPlugin->setParameter(0, 0.25f);
    const auto state = hostPlugin->getExtraState();
    REQUIRE(state != guestPlugin->getExtraState());
    host.coop->beginTransaction("Plugin State", false, false);
    auto effect = host.session.getEffect(effectId);
    REQUIRE(effect.has_value());
    effect->setExtraState(state);
    shared.world.settle();
    CHECK(guestPlugin->getExtraState() == state);
    CHECK(guestPlugin->getParameter(0) == 0.25f);

    // A computer without the plugin keeps its settings and names it.
    auto& third = shared.world.add({false, true, false});
    const auto invite = controller::parseInvite(host.coop->getInviteLink());
    REQUIRE(invite.has_value());
    TemporaryDirectory thirdFolder;
    REQUIRE(third.coop->join(*invite, "Lu", thirdFolder.file("Copy")).wasOk());
    shared.world.settle();
    shared.world.run(0.5);
    CHECK(third.coop->takeNotices().joinIntoString(" ").contains("is not installed on this computer"));
    CHECK(third.session.getEffect(effectId)->getExtraState() == state);
}
