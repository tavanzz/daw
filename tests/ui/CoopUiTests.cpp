#include "daw/controller/CoopSession.h"
#include "daw/server/Server.h"
#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/CoopPanels.h"
#include "daw/ui/desktop/MainComponent.h"
#include "daw/ui/desktop/ThemeManager.h"

#include "support/SimulatedNetwork.h"
#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace daw;
using ui::desktop::CoopSessionPanel;
using ui::desktop::CoopStartPanel;
using ui::desktop::MainComponent;

namespace
{

const juce::String hostKey = "ui test key";
const juce::String fingerprint = juce::String::repeatedString("A", 43);

void snapshot(juce::Component& component, const juce::String& name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable("DAW_UI_SNAPSHOTS", {});
    if (folder.isEmpty())
        return;
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
    const juce::File file = juce::File(folder).getChildFile(name + ".png");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat().writeImageToStream(image, stream);
}

// A computer in the session: its session and its part in the co-op session, over the
// simulated network to the server.
struct Computer
{
    Computer(test::SimulatedNetwork& network, server::Server& server)
    {
        engine.prepare(48000.0, 512);
        session.setSampleRate(48000.0);
        controller::CoopSession::Options options;
        options.appVersion = "test";
        options.clock = [&network] { return network.now(); };
        coop = std::make_unique<controller::CoopSession>(
            session,
            [&network, &server](const controller::CoopServer&)
            {
                auto pair = network.connect({0.005, 0.02, 0.0, 0.0, true});
                server.addConnection(std::move(pair.server), "10.0.0.1", network.now());
                return std::move(pair.client);
            },
            options);
    }

    engine::Engine engine;
    controller::Session session{engine, {engine::io::DiskStreamer::Mode::manual}};
    std::unique_ptr<controller::CoopSession> coop;
};

struct World
{
    World()
    {
        options.hostKeys.add(hostKey);
        server = std::make_unique<server::Server>(options);
    }

    Computer& add()
    {
        computers.push_back(std::make_unique<Computer>(network, *server));
        return *computers.back();
    }

    void run(double seconds)
    {
        for (double time = 0.0; time < seconds; time += 0.01)
        {
            for (auto& computer : computers)
            {
                computer->coop->update();
                computer->session.update();
            }
            server->update(network.now());
            network.advance(0.01);
        }
    }

    [[nodiscard]] static juce::String serverLink(const juce::String& key = hostKey)
    {
        return controller::serverLink({{"server.test", 7443, fingerprint}, key});
    }

    test::SimulatedNetwork network{5};
    server::ServerOptions options;
    std::unique_ptr<server::Server> server;
    std::vector<std::unique_ptr<Computer>> computers;
};

struct Desktop
{
    Desktop(Computer& computer, const juce::File& folder)
        : settings(folder.getChildFile("Settings"), "MICINSTATION"),
          view(computer.session, devices, commands, themes)
    {
        commands.registerAllCommandsForTarget(&view);
        commands.setFirstCommandTarget(&view);
        view.setSize(1400, 900);
        ui::desktop::AppServices services;
        services.settings = &settings;
        services.coopFolder = folder.getChildFile("Co-op");
        view.setAppServices(services);
        view.setCoopSession(computer.coop.get());
    }

    [[nodiscard]] bool isActive(juce::CommandID command)
    {
        juce::ApplicationCommandInfo info(command);
        view.getCommandInfo(command, info);
        return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
    }

    services::UserSettings settings;
    ui::desktop::ThemeManager themes{{}};
    juce::AudioDeviceManager devices;
    juce::ApplicationCommandManager commands;
    MainComponent view;
};

} // namespace

TEST_CASE("The co-op windows host a session, hand out its invite and list who is in it", "[ui][coop]")
{
    test::TemporaryDirectory folder;
    World world;
    auto& host = world.add();
    auto& guest = world.add();
    Desktop desktop(host, folder.file("Host"));

    CHECK(desktop.isActive(MainComponent::coopHost));
    CHECK(desktop.isActive(MainComponent::coopJoin));
    CHECK_FALSE(desktop.isActive(MainComponent::coopLeave));
    CHECK_FALSE(desktop.isActive(MainComponent::coopPlayForEveryone));

    desktop.view.showCoopStart(CoopStartPanel::Mode::host, World::serverLink());
    auto* start = desktop.view.findCoopStartPanel();
    REQUIRE(start != nullptr);
    start->setParticipantName("Ann");
    start->press("Host");
    CHECK(start->isWaiting());
    world.run(1.0);
    start->poll();
    CHECK_FALSE(start->isWaiting());
    REQUIRE(host.coop->isActive());
    REQUIRE(host.coop->getClient().getStatus() == coop::Client::Status::connected);
    CHECK(host.coop->getCopyFolder().getParentDirectory() == folder.file("Host/Co-op"));

    // The name and the server link are remembered for the next time.
    CHECK(desktop.settings.getCoopPreferences().name == "Ann");
    CHECK(desktop.settings.getCoopPreferences().hostLink == World::serverLink());

    CHECK_FALSE(desktop.isActive(MainComponent::coopHost));
    CHECK(desktop.isActive(MainComponent::coopLeave));
    CHECK(desktop.isActive(MainComponent::coopClose));
    CHECK(desktop.isActive(MainComponent::coopCopyInvite));

    desktop.view.showCoopSession();
    auto* panel = desktop.view.findCoopSessionPanel();
    REQUIRE(panel != nullptr);
    panel->refresh();
    CHECK(panel->getStatus().startsWith("Connected"));
    CHECK(panel->getInvite().startsWith("micinstation://join?"));
    CHECK(panel->getParticipantLines() == juce::StringArray{"Ann (you, host)"});

    const auto invite = controller::parseInvite(panel->getInvite());
    REQUIRE(invite.has_value());
    REQUIRE(guest.coop->join(*invite, "Marco", folder.file("Guest")).wasOk());
    world.run(2.0);
    REQUIRE(guest.coop->getClient().getStatus() == coop::Client::Status::connected);

    panel->refresh();
    CHECK(panel->getParticipantLines() == juce::StringArray{"Ann (you, host)", "Marco"});
    CHECK(panel->getButtonTexts().contains("Remove Marco"));
    CHECK(panel->getButtonTexts().contains("Make Marco Host"));
    desktop.view.getStatusBarForTesting().refresh();
    CHECK(desktop.view.getStatusBarForTesting().getLine().coop == "Co-op: 2 here");
    CHECK(desktop.view.getStatusBarForTesting().getLine().coopPeople.size() == 2);

    // The guest sees no host buttons, and no invite to hand out.
    CoopSessionPanel guestPanel(*guest.coop, {});
    guestPanel.setBounds(guestPanel.getLocalBounds());
    CHECK(guestPanel.getParticipantLines() == juce::StringArray{"Ann (host)", "Marco (you)"});
    CHECK_FALSE(guestPanel.getButtonTexts().contains("Remove Ann"));
    CHECK_FALSE(guestPanel.getButtonTexts().contains("Close Session"));
    CHECK(guestPanel.getInvite().isEmpty());

    for (const auto& theme : desktop.themes.getLibrary().getThemes())
    {
        desktop.themes.setTheme(theme.id);
        CoopSessionPanel themed(*host.coop, {});
        themed.setBounds(themed.getLocalBounds());
        CHECK(themed.createComponentSnapshot(themed.getLocalBounds()).isValid());
        snapshot(themed, "coop-session-" + theme.id);
        CHECK(desktop.view.getStatusBarForTesting().createComponentSnapshot({0, 0, 1400, 24}).isValid());
    }

    // A new code replaces the invite: the old one stops working.
    panel->press("New Code");
    world.run(1.0);
    panel->refresh();
    CHECK(panel->getInvite() != controller::inviteLink(*invite));

    // The host removes Marco: his copy stays, out of the session.
    panel->press("Remove Marco");
    world.run(2.0);
    CHECK(guest.coop->getClient().getStatus() == coop::Client::Status::ended);
    CHECK_FALSE(guest.coop->isActive());
    panel->refresh();
    CHECK(panel->getParticipantLines() == juce::StringArray{"Ann (you, host)"});

    host.coop->leave();
    CHECK(desktop.isActive(MainComponent::coopHost));
    CHECK_FALSE(desktop.isActive(MainComponent::coopLeave));
    panel->refresh();
    CHECK(panel->getButtonTexts().contains("Close"));
}

TEST_CASE("The co-op start window explains a wrong link and the server's refusal", "[ui][coop]")
{
    test::TemporaryDirectory folder;
    World world;
    auto& computer = world.add();
    Desktop desktop(computer, folder.file("Here"));

    desktop.view.showCoopStart(CoopStartPanel::Mode::join, "https://example.com/?code=x");
    auto* join = desktop.view.findCoopStartPanel();
    REQUIRE(join != nullptr);
    join->setParticipantName("Lu");
    join->press("Join");
    CHECK_FALSE(join->isWaiting());
    CHECK(join->getMessage().contains("not an invite"));
    CHECK_FALSE(computer.coop->isActive());

    desktop.view.showCoopStart(CoopStartPanel::Mode::host, World::serverLink("a wrong key"));
    auto* start = desktop.view.findCoopStartPanel();
    REQUIRE(start != nullptr);
    start->setParticipantName({});
    start->press("Host");
    CHECK(start->getMessage().contains("name"));

    start->setParticipantName("Lu");
    start->press("Host");
    world.run(1.0);
    start->poll();
    CHECK_FALSE(start->isWaiting());
    CHECK(start->getMessage().contains("host key"));
    CHECK_FALSE(computer.coop->isActive());
    CHECK(desktop.isActive(MainComponent::coopHost));
}

TEST_CASE("Participant chips draw every colour with a readable initial in every theme", "[ui][coop]")
{
    ui::desktop::ThemeManager themes({});
    for (const auto& theme : themes.getLibrary().getThemes())
    {
        themes.setTheme(theme.id);
        juce::Component holder;
        holder.setSize(8 * 24, 24);
        juce::Image image(juce::Image::ARGB, holder.getWidth(), holder.getHeight(), true);
        juce::Graphics g(image);
        for (int colour = 0; colour < 8; ++colour)
            ui::desktop::drawParticipantChip(g, holder, juce::Rectangle<float>(static_cast<float>(colour * 24), 0.0f, 22.0f, 22.0f),
                                             colour, "Ann");
        // Each chip is filled with its participant's colour.
        const auto& look = ui::desktop::ThemedLookAndFeel::get(holder);
        for (int colour = 0; colour < 8; ++colour)
            CHECK(image.getPixelAt(colour * 24 + 3, 18) == look.participantColour(colour));
    }
}
