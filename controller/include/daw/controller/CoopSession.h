#pragma once

#include "daw/controller/Collaboration.h"
#include "daw/controller/Session.h"
#include "daw/coop/Client.h"
#include "daw/coop/Links.h"
#include "daw/net/Link.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>

namespace daw::controller
{

using CoopServer = coop::ServerAddress;
using CoopInvite = coop::JoinLink;
using CoopServerLink = coop::HostLink;

[[nodiscard]] inline juce::String inviteLink(const CoopInvite& invite)
{
    return coop::toText(invite);
}
[[nodiscard]] inline juce::String serverLink(const CoopServerLink& link)
{
    return coop::toText(link);
}
/** Reads a link as people paste it (spaces and line breaks around it do not matter). */
[[nodiscard]] inline std::optional<CoopInvite> parseInvite(const juce::String& text)
{
    return coop::parseJoinLink(text);
}
[[nodiscard]] inline std::optional<CoopServerLink> parseServerLink(const juce::String& text)
{
    return coop::parseHostLink(text);
}

/** What a copy remembers of its session (.coop/session.json in its folder, readable by
    its owner only), to come back after the app started again. */
struct CoopMembership
{
    CoopServer server;
    juce::String sessionId;
    coop::ParticipantNumber participant = 0;
    juce::String token;
    juce::String name;
};

[[nodiscard]] std::optional<CoopMembership> readMembership(const juce::File& copyFolder);

/** A new folder in parent for the co-op copy of a project called title: "Song", or
    "Song 2" if that one exists, and so on (names a file system refuses are made legal). */
[[nodiscard]] juce::File newCopyFolder(const juce::File& parent, const juce::String& title);
juce::Result writeMembership(const juce::File& copyFolder, const CoopMembership& membership);

/**
    A Session in a co-op session: the controller's side of coop::Client, for every UI.

    Hosting makes a copy of the open project in a folder of its own (its files copied
    into "Audio/1", the original left as it was) and opens it; joining starts from an empty
    project that the session's replaces. Edits of the Session then go through the locks
    and the personal undo (Collaboration), changes of others reach the engine the way
    local ones do, files arrive and load, "Play for Everyone" starts this computer at the
    moment the server named, and the copy is saved on its own.

    update() before Session::update(), on the message thread.
*/
class CoopSession final : public Collaboration
{
public:
    using LinkFactory = std::function<std::unique_ptr<net::Link>(const CoopServer&)>;

    struct Options
    {
        juce::String appVersion;
        /** Seconds on the clock the engine stamps its blocks with (midi::hostSeconds()). */
        std::function<double()> clock;
        double autosaveInterval = 30.0;
        coop::Client::Options client;
    };

    /** TLS 1.3 links checking the server's fingerprint. */
    [[nodiscard]] static LinkFactory tlsLinks();

    CoopSession(Session& session, LinkFactory links, Options options);
    ~CoopSession() override;

    CoopSession(const CoopSession&) = delete;
    CoopSession& operator=(const CoopSession&) = delete;

    /** Opens a session with a copy of the Session's project in copyFolder. */
    juce::Result host(const CoopServerLink& link, const juce::String& name, const juce::File& copyFolder);
    /** Joins with an invite; the Session's project becomes the session's, in copyFolder. */
    juce::Result join(const CoopInvite& invite, const juce::String& name, const juce::File& copyFolder);
    /** Comes back to the session of the copy in copyFolder (after the app started again). */
    juce::Result rejoin(const juce::File& copyFolder);
    /** Leaves for good; the copy stays, an ordinary project. */
    void leave();

    void update();

    /** Saves the copy (it is saved on its own every autosaveInterval). */
    juce::Result saveCopy();

    [[nodiscard]] coop::Client& getClient() noexcept { return *client; }
    [[nodiscard]] const coop::Client& getClient() const noexcept { return *client; }
    /** The client of the last session started here (it stays, ended, after leaving): null
        if none was. */
    [[nodiscard]] const coop::Client* findClient() const noexcept { return client.get(); }
    /** The project's name, as invites carry it. */
    [[nodiscard]] juce::String getTitle() const;
    [[nodiscard]] const CoopServer& getServer() const noexcept { return server; }
    [[nodiscard]] Session& getSession() noexcept { return session; }
    [[nodiscard]] const juce::File& getCopyFolder() const noexcept { return folder; }
    [[nodiscard]] bool isActive() const noexcept;
    /** The invite as a link to paste in a chat; empty without a code. */
    [[nodiscard]] juce::String getInviteLink() const;
    /** How long the invite stays valid, in seconds (nothing without one, or a clock). */
    [[nodiscard]] std::optional<double> getInviteSecondsLeft() const;

    /** The state of a file of the copy (incoming, downloading with progress...). */
    [[nodiscard]] std::optional<coop::FileSync::State> getFileState(const juce::File& file) const;

    /** Notices for the user since the last call (refused edits, missing plugins...). */
    [[nodiscard]] juce::StringArray takeNotices();

    // Presence and the shared transport --------------------------------------------------

    /** What this participant looks at; the playhead is filled in here. */
    void setPresence(coop::Presence presence);

    /** Everyone who follows starts from position at the same moment (a little later). */
    void playForEveryone(model::Ticks position);
    void stopForEveryone();
    void setFollowing(bool shouldFollow) noexcept { following = shouldFollow; }
    [[nodiscard]] bool isFollowing() const noexcept { return following; }

    // Collaboration ----------------------------------------------------------------------

    bool mayEdit(const std::vector<model::EditScope>& scopes, juce::String& why) override;
    [[nodiscard]] bool holds(const model::EditScope& scope) const override;
    void beginTransaction(const juce::String& name, bool undoable, bool continuesStep) override;
    void beginGesture() override;
    void endGesture() override;
    bool undo() override;
    bool redo() override;
    [[nodiscard]] bool canUndo() const override;
    [[nodiscard]] bool canRedo() const override;
    [[nodiscard]] juce::String getUndoName() const override;
    [[nodiscard]] juce::String getRedoName() const override;
    [[nodiscard]] juce::File getOwnFilesFolder() const override;
    void addOwnFile(const juce::File& file) override;
    [[nodiscard]] bool isFileReady(const juce::File& file) const override;
    void holdLocks(const std::vector<model::EditScope>& scopes) override;
    void setRecording(bool recording) override;

private:
    void start(const CoopServer& serverToUse, const juce::File& copyFolder);
    void stopCollaborating();
    void addOwnFiles(const juce::ValueTree& tree);
    void startTransport(const coop::TransportStart& start);
    void watchPlugins();
    [[nodiscard]] juce::String relativePathOf(const juce::File& file) const;
    [[nodiscard]] double now() const;

    Session& session;
    LinkFactory makeLink;
    Options options;
    CoopServer server;
    juce::File folder;
    juce::String participantName;
    std::unique_ptr<coop::Client> client;
    juce::ValueTree state;

    std::uint64_t currentStep = 0;
    bool following = true;
    bool membershipWritten = false;
    double savedAt = 0.0;
    std::uint64_t savedSequence = 0;
    juce::StringArray notices;
    juce::StringArray unavailablePlugins;
    std::optional<coop::Presence> presence;
};

} // namespace daw::controller
