// What a co-op session costs (Phase 9, docs/PERFORMANCE.md): the bandwidth each participant
// uses while idle, while everyone moves around, while everyone edits and in a typical mix;
// how long an edit takes to reach the others; the CPU time of the server and of a client;
// the size of a project when someone joins. The server's real logic and the clients' real
// synchronisation run on the simulated network (its clock, delays of 20 to 40 ms each way,
// in order like TCP), so the figures are the protocol's, not a machine's or a network's.
// Not a test: run it in Release, build/release/tests/daw_coop_bench.

#include "daw/coop/Client.h"
#include "daw/coop/Messages.h"
#include "daw/model/Project.h"
#include "daw/model/SharedProject.h"
#include "daw/server/Server.h"

#include "support/SimulatedNetwork.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace daw;

const juce::String hostKey = "benchmark host key";
constexpr int participantsInSession = 4;
constexpr double measuredSeconds = 120.0;
constexpr double step = 0.005; // the clock's step: a fifth of the shortest delay

// What one end sent and received over its links (both of a participant's connections).
struct Traffic
{
    std::uint64_t upBytes = 0, downBytes = 0, upFrames = 0, downFrames = 0;
};

class CountingLink final : public net::Link
{
public:
    CountingLink(std::unique_ptr<net::Link> linkToCount, Traffic& countsToKeep)
        : inner(std::move(linkToCount)), counts(countsToKeep)
    {
    }

    void send(juce::MemoryBlock frame) override
    {
        counts.upBytes += frame.getSize();
        ++counts.upFrames;
        inner->send(std::move(frame));
    }

    std::vector<juce::MemoryBlock> receive() override
    {
        auto frames = inner->receive();
        for (const auto& frame : frames)
        {
            counts.downBytes += frame.getSize();
            ++counts.downFrames;
        }
        return frames;
    }

    State getState() const override { return inner->getState(); }
    juce::String getProblem() const override { return inner->getProblem(); }
    void close() override { inner->close(); }

private:
    std::unique_ptr<net::Link> inner;
    Traffic& counts;
};

double seconds()
{
    return juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks());
}

// A project of a band: audio tracks with takes, MIDI tracks with long clips, two effects
// on every track, two buses.
void buildProject(model::Project& project, const juce::File& folder, int audioTracks, int midiTracks,
                  int notesPerClip)
{
    juce::Random random(7);
    for (int t = 0; t < audioTracks; ++t)
    {
        auto track = project.addTrack("Audio " + juce::String(t + 1));
        for (int c = 0; c < 8; ++c)
            track.addClip(folder.getChildFile("Audio/1/take " + juce::String(t * 8 + c + 1) + ".wav"),
                          {model::ticksPerSecond * 10 * c, 0, model::ticksPerSecond * 8});
        track.getEffects().add("equaliser", 0);
        track.getEffects().add("compressor", 1);
    }
    for (int t = 0; t < midiTracks; ++t)
    {
        auto track = project.addTrack("MIDI " + juce::String(t + 1), model::TrackKind::midi);
        for (int c = 0; c < 4; ++c)
        {
            std::vector<model::Note> notes;
            for (int n = 0; n < notesPerClip; ++n)
                notes.push_back({n + 1, model::ticksPerSecond / 8 * n, model::ticksPerSecond / 10,
                                 36 + random.nextInt(48), 40 + random.nextInt(87)});
            track.addMidiClip("Part " + juce::String(c + 1),
                              {model::ticksPerSecond * 40 * c, 0, model::ticksPerSecond * 32}, std::move(notes));
        }
        track.getEffects().add("delay", 0);
        track.getEffects().add("reverb", 1);
    }
    project.addBus("Reverb");
    project.addBus("Delay");
}

// What the participants do, per scenario.
struct Behaviour
{
    double pointerShare = 0.0;   ///< of the time each one moves the pointer (60 moves a second)
    double playingShare = 0.0;   ///< of the time one of them plays (the playhead moves)
    double editInterval = 0.0;   ///< seconds between discrete edits of each (0: none)
    double gestureInterval = 0.0; ///< seconds between fader drags of a second (0: none)
};

struct Participant
{
    model::Project project;
    juce::File folder;
    Traffic traffic;
    std::unique_ptr<coop::Client> client;
    coop::Presence presence;
    double nextEdit = 0.0;
    double nextGesture = 0.0;
    double gestureEnd = -1.0;
    double cpu = 0.0; ///< seconds in update()
};

class World
{
public:
    explicit World(int notesPerClip = 200) : network(11)
    {
        options.hostKeys.add(hostKey);
        server = std::make_unique<server::Server>(options);
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("daw-coop-bench-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));

        for (int index = 0; index < participantsInSession; ++index)
        {
            auto participant = std::make_unique<Participant>();
            participant->folder = root.getChildFile("p" + juce::String(index + 1));
            auto* who = participant.get();
            participant->client = std::make_unique<coop::Client>(
                participant->project.getState(), participant->folder,
                [this, who] { return connect(*who); }, coop::Client::Options{});
            participants.push_back(std::move(participant));
        }

        auto& host = *participants[0];
        buildProject(host.project, host.folder, 12, 4, notesPerClip);
        host.client->host(hostKey, "Host");
        run(1.0, {});
        for (std::size_t index = 1; index < participants.size(); ++index)
            participants[index]->client->join(host.client->getInvite()->code, "P" + juce::String(static_cast<int>(index)));
        run(3.0, {});
        for (auto& participant : participants)
            if (participant->client->getStatus() != coop::Client::Status::connected)
                std::printf("participant not connected!\n");
    }

    ~World() { root.deleteRecursively(); }

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    std::unique_ptr<net::Link> connect(Participant& who)
    {
        auto pair = network.connect({0.02, 0.04, 0.0, 0.0, true});
        server->addConnection(std::move(pair.server), "10.0.0." + juce::String(++connections), network.now());
        return std::make_unique<CountingLink>(std::move(pair.client), who.traffic);
    }

    void run(double duration, const Behaviour& behaviour)
    {
        const auto end = network.now() + duration;
        while (network.now() < end)
        {
            act(behaviour);
            for (auto& participant : participants)
            {
                const auto start = seconds();
                participant->client->update(network.now());
                participant->cpu += seconds() - start;
            }
            const auto start = seconds();
            server->update(network.now());
            serverCpu += seconds() - start;
            network.advance(step);
        }
    }

    void resetCounts()
    {
        for (auto& participant : participants)
        {
            participant->traffic = {};
            participant->cpu = 0.0;
        }
        serverCpu = 0.0;
    }

    // Each participant works on tracks of its own (so nobody's edit is refused): an audio
    // track (index * 3) and a MIDI track (12 + index).
    void act(const Behaviour& behaviour)
    {
        const auto now = network.now();
        for (std::size_t index = 0; index < participants.size(); ++index)
        {
            auto& participant = *participants[index];
            auto& client = *participant.client;
            auto& project = participant.project;
            const int ownTrack = static_cast<int>(index) * 3;

            // The pointer moves in bursts, the playhead of whoever plays at every step.
            auto presence = participant.presence;
            const double phase = std::fmod(now + static_cast<double>(index) * 7.0, 20.0) / 20.0;
            if (phase < behaviour.pointerShare && std::fmod(now, 1.0 / 60.0) < step)
            {
                presence.hasCursor = true;
                presence.cursorTrack = project.getTrack(ownTrack).getId();
                presence.cursorPosition = static_cast<model::Ticks>(now * 1000.0) * model::ticksPerSecond / 1000;
            }
            presence.playing = index == 0 && std::fmod(now, 60.0) / 60.0 < behaviour.playingShare;
            if (presence.playing)
                presence.playhead = static_cast<model::Ticks>(now * 1000.0) * model::ticksPerSecond / 1000;
            if (!(presence == participant.presence))
            {
                participant.presence = presence;
                client.setPresence(presence);
            }

            if (behaviour.editInterval > 0.0 && now >= participant.nextEdit)
            {
                participant.nextEdit = now + behaviour.editInterval * (0.5 + random.nextDouble());
                edit(participant, ownTrack);
            }

            if (behaviour.gestureInterval > 0.0 && now >= participant.nextGesture)
            {
                participant.nextGesture = now + behaviour.gestureInterval * (0.5 + random.nextDouble());
                participant.gestureEnd = now + 1.0;
                client.setTransaction("Volume", client.newUndoStep());
                client.setFlushInterval(0.1);
            }
            if (participant.gestureEnd >= 0.0)
            {
                if (now < participant.gestureEnd)
                {
                    if (std::fmod(now, 1.0 / 60.0) < step)
                        project.getTrack(ownTrack).setVolume(0.2 + 0.6 * (participant.gestureEnd - now));
                }
                else
                {
                    participant.gestureEnd = -1.0;
                    client.flush();
                    client.setFlushInterval(0.0);
                }
            }
        }
    }

    void edit(Participant& participant, int ownTrack)
    {
        auto& client = *participant.client;
        auto& project = participant.project;
        const int kind = random.nextInt(3);
        client.setTransaction(kind == 0 ? "Move Clip" : kind == 1 ? "Move Note" : "Pan", client.newUndoStep());
        if (kind == 0)
        {
            auto track = project.getTrack(ownTrack);
            auto clip = track.getClip(random.nextInt(track.getNumClips()));
            clip.setPosition(clip.getPosition() + model::ticksPerSecond / 4);
        }
        else if (kind == 1)
        {
            // A note of a MIDI clip moves: the clip's notes travel as the range that changed.
            auto track = project.getTrack(12 + ownTrack / 3);
            model::MidiClip clip(track.getClip(random.nextInt(track.getNumClips())).getState());
            auto notes = clip.getNotes();
            if (!notes.empty())
            {
                auto& note = notes[static_cast<std::size_t>(random.nextInt(static_cast<int>(notes.size())))];
                note.key = std::clamp(note.key + (random.nextBool() ? 1 : -1), 0, 127);
                clip.setNotes(std::move(notes));
            }
        }
        else
            project.getTrack(ownTrack).setPan(random.nextDouble() * 2.0 - 1.0);
        client.flush();
    }

    test::SimulatedNetwork network;
    server::ServerOptions options;
    std::unique_ptr<server::Server> server;
    std::vector<std::unique_ptr<Participant>> participants;
    juce::File root;
    juce::Random random{3};
    int connections = 0;
    double serverCpu = 0.0;
};

// Over the wire each frame also costs its length (4 bytes), a TLS 1.3 record (22) and, at
// worst, a TCP/IP packet of its own (about 52, an acknowledgement included).
constexpr double wireOverheadPerFrame = 4.0 + 22.0 + 52.0;

void report(const char* name, const Behaviour& behaviour)
{
    World world;
    world.resetCounts();
    world.run(measuredSeconds, behaviour);

    double up = 0.0, down = 0.0, upWire = 0.0, downWire = 0.0, cpu = 0.0;
    for (const auto& participant : world.participants)
    {
        const auto& traffic = participant->traffic;
        up += static_cast<double>(traffic.upBytes);
        down += static_cast<double>(traffic.downBytes);
        upWire += static_cast<double>(traffic.upBytes) + wireOverheadPerFrame * static_cast<double>(traffic.upFrames);
        downWire += static_cast<double>(traffic.downBytes) + wireOverheadPerFrame * static_cast<double>(traffic.downFrames);
        cpu += participant->cpu;
    }
    const auto perParticipant = [](double total) { return total / participantsInSession / measuredSeconds; };
    std::printf("| %-34s | %7.0f | %7.0f | %8.0f | %8.0f | %9.0f | %6.3f | %6.3f |\n", name, perParticipant(up),
                perParticipant(down), perParticipant(upWire), perParticipant(downWire),
                (upWire + downWire) / measuredSeconds, 1000.0 * world.serverCpu / measuredSeconds,
                1000.0 * cpu / participantsInSession / measuredSeconds);
}

void reportDelays()
{
    // The host turns a track up or down every two seconds while everyone moves around:
    // how long until each other copy has it?
    World world;
    Behaviour background{0.5, 0.5, 0.0, 0.0};
    world.run(2.0, background);
    std::vector<double> delays;
    auto& host = *world.participants[0];
    for (int edit = 0; edit < 30; ++edit)
    {
        const auto sessionId = world.server->getSessionIds()[0];
        const auto before = world.server->getSequence(sessionId);
        host.client->setTransaction("Volume", host.client->newUndoStep());
        host.project.getTrack(0).setVolume(edit % 2 == 0 ? 0.5 : 0.7);
        host.client->flush();
        const auto sent = world.network.now();
        std::vector<bool> arrived(world.participants.size(), false);
        arrived[0] = true;
        while (std::find(arrived.begin(), arrived.end(), false) != arrived.end() && world.network.now() < sent + 5.0)
        {
            world.run(step, background);
            for (std::size_t index = 1; index < world.participants.size(); ++index)
                if (!arrived[index] && world.participants[index]->client->getSequence() > before)
                {
                    arrived[index] = true;
                    delays.push_back(world.network.now() - sent);
                }
        }
        world.run(2.0, background);
    }
    std::sort(delays.begin(), delays.end());
    const auto at = [&delays](double share) {
        return 1000.0 * delays[std::min(delays.size() - 1, static_cast<std::size_t>(share * static_cast<double>(delays.size())))];
    };
    std::printf("Edit to the other copies (%d arrivals, 20 to 40 ms each way): shortest %.0f ms, median %.0f ms, "
                "90 %% %.0f ms, longest %.0f ms\n",
                static_cast<int>(delays.size()), at(0.0), at(0.5), at(0.9), at(1.0));
}

void reportSnapshot(const char* name, int audioTracks, int midiTracks, int notesPerClip)
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("daw-coop-bench-snapshot");
    model::Project project;
    buildProject(project, folder, audioTracks, midiTracks, notesPerClip);
    coop::ProjectSnapshot snapshot;
    snapshot.sequence = 1;
    snapshot.project = model::sharedCopy(project.getState(), folder);
    const auto start = seconds();
    const auto bytes = coop::encode(coop::Message{snapshot});
    const auto encoding = seconds() - start;
    std::printf("| %-50s | %9d | %8.1f |\n", name, static_cast<int>(bytes.getSize()), 1000.0 * encoding);
}

} // namespace

int main()
{
    std::printf("Co-op: %d participants, %.0f s of session each, one-way delays of 20 to 40 ms\n\n",
                participantsInSession, measuredSeconds);
    std::printf("Bytes a second per participant (frames, then on the wire at most), the server's total, CPU ms a second\n\n");
    std::printf("| %-34s | %7s | %7s | %8s | %8s | %9s | %6s | %6s |\n", "Scenario", "up", "down", "up wire",
                "down wire", "server", "server", "client");
    std::printf("|%s|---------|---------|----------|----------|-----------|--------|--------|\n",
                std::string(36, '-').c_str());
    report("Idle", {});
    report("Everyone moves the pointer", {1.0, 1.0, 0.0, 0.0});
    report("Everyone edits (2 s), drags (10 s)", {0.0, 0.0, 2.0, 10.0});
    report("Typical (half moving, edits 5 s)", {0.5, 0.5, 5.0, 20.0});

    std::printf("\n");
    reportDelays();

    std::printf("\nProject sent to whoever joins (zlib), and the time to encode it\n\n");
    std::printf("| %-50s | %9s | %8s |\n", "Project", "bytes", "ms");
    std::printf("|%s|-----------|----------|\n", std::string(52, '-').c_str());
    reportSnapshot("16 tracks, 96 takes, 16 MIDI clips x 200 notes", 12, 4, 200);
    reportSnapshot("64 tracks, 384 takes, 64 MIDI clips x 1000 notes", 48, 16, 1000);
    return 0;
}
