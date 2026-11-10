#pragma once

#include "daw/coop/Client.h"
#include "daw/model/Project.h"
#include "daw/model/SharedProject.h"
#include "daw/server/Server.h"

#include "SimulatedNetwork.h"

#include <memory>
#include <vector>

namespace daw::test
{

/**
    A co-op session for tests: a server and participants (coop::Client, each with a project
    and a copy folder of its own) on a simulated network whose conditions can change.
*/
class CoopWorld
{
public:
    static inline const juce::String hostKey = "test host key";

    struct Participant
    {
        model::Project project;
        juce::File folder;
        std::unique_ptr<coop::Client> client;
        double clockOffset = 0.0; ///< this computer's clock: the network's plus this
    };

    explicit CoopWorld(juce::int64 seed, server::ServerOptions options = {})
        : network(seed),
          serverOptions(withHostKey(std::move(options))),
          serverPointer(std::make_unique<server::Server>(serverOptions))
    {
    }

    ~CoopWorld() { root.deleteRecursively(); }

    CoopWorld(const CoopWorld&) = delete;
    CoopWorld& operator=(const CoopWorld&) = delete;

    /** The server stops (what it saved stays) and starts again: every link breaks. */
    void restartServer()
    {
        serverPointer.reset();
        network.cutAll();
        serverPointer = std::make_unique<server::Server>(serverOptions);
    }

    Participant& add(const juce::String& participantName = {})
    {
        auto participant = std::make_unique<Participant>();
        participant->folder = root.getChildFile(participantName.isNotEmpty()
                                                    ? participantName
                                                    : "p" + juce::String(static_cast<int>(participants.size())));
        coop::Client::Options options;
        options.appVersion = "test";
        participant->client = std::make_unique<coop::Client>(
            participant->project.getState(), participant->folder,
            [this] { return connect(); }, options);
        participants.push_back(std::move(participant));
        return *participants.back();
    }

    std::unique_ptr<net::Link> connect()
    {
        auto pair = network.connect(conditions);
        serverPointer->addConnection(std::move(pair.server),
                                     "10.0.0." + juce::String(++connections % 200), network.now());
        return std::move(pair.client);
    }

    /** Runs everyone for a while, in steps. */
    void run(double seconds, double step = 0.01)
    {
        for (double time = 0.0; time < seconds; time += step)
            tick(step);
    }

    void tick(double step = 0.01)
    {
        for (auto& participant : participants)
            participant->client->update(network.now() + participant->clockOffset);
        serverPointer->update(network.now());
        network.advance(step);
    }

    /** True once nothing waits: every participant has the server's last transaction. */
    bool isQuiet() const
    {
        if (serverPointer->getSessionIds().isEmpty())
            return true;
        const auto sequence = serverPointer->getSequence(serverPointer->getSessionIds()[0]);
        for (const auto& participant : participants)
        {
            const auto& client = *participant->client;
            if (client.getStatus() != coop::Client::Status::connected || client.getNumPending() > 0
                || client.getSequence() != sequence)
                return false;
        }
        return true;
    }

    [[nodiscard]] juce::ValueTree serverProject() const
    {
        return serverPointer->getProject(serverPointer->getSessionIds()[0]);
    }

    [[nodiscard]] server::Server& server() noexcept { return *serverPointer; }
    [[nodiscard]] std::uint64_t serverSequence() const
    {
        return serverPointer->getSequence(serverPointer->getSessionIds()[0]);
    }

    // Folders of this world's copies: tests run in parallel processes, each with its own.
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("daw-coop-world-"
                                        + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));

    SimulatedNetwork network;
    SimulatedNetwork::Conditions conditions;
    server::ServerOptions serverOptions;
    std::unique_ptr<server::Server> serverPointer;
    std::vector<std::unique_ptr<Participant>> participants;
    int connections = 0;

private:
    static server::ServerOptions withHostKey(server::ServerOptions options)
    {
        options.hostKeys.add(hostKey);
        return options;
    }
};

} // namespace daw::test
