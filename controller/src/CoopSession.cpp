#include "daw/controller/CoopSession.h"

#include "daw/controller/SharedTransport.h"
#include "daw/engine/midi/LiveInput.h"
#include "daw/model/ProjectFile.h"
#include "daw/model/SharedProject.h"
#include "daw/net/TlsLink.h"

#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#include <algorithm>
#include <map>

namespace daw::controller
{

namespace
{

constexpr const char* membershipFile = ".coop/session.json";

} // namespace

std::optional<CoopMembership> readMembership(const juce::File& copyFolder)
{
    const auto json = juce::JSON::parse(copyFolder.getChildFile(membershipFile));
    if (!json.isObject())
        return std::nullopt;
    CoopMembership membership;
    membership.server.host = json["host"].toString();
    membership.server.port = static_cast<int>(json["port"]);
    membership.server.fingerprint = json["fingerprint"].toString();
    membership.sessionId = json["sessionId"].toString();
    membership.participant = static_cast<coop::ParticipantNumber>(static_cast<juce::int64>(json["participant"]));
    membership.token = json["token"].toString();
    membership.name = json["name"].toString();
    if (membership.server.host.isEmpty() || membership.sessionId.isEmpty() || membership.token.isEmpty())
        return std::nullopt;
    return membership;
}

juce::Result writeMembership(const juce::File& copyFolder, const CoopMembership& membership)
{
    auto* object = new juce::DynamicObject();
    const juce::var json(object);
    object->setProperty("host", membership.server.host);
    object->setProperty("port", membership.server.port);
    object->setProperty("fingerprint", membership.server.fingerprint);
    object->setProperty("sessionId", membership.sessionId);
    object->setProperty("participant", static_cast<juce::int64>(membership.participant));
    object->setProperty("token", membership.token);
    object->setProperty("name", membership.name);

    const auto file = copyFolder.getChildFile(membershipFile);
    if (!file.getParentDirectory().createDirectory()
        || !file.replaceWithText(juce::JSON::toString(json), false, false, "\n"))
        return juce::Result::fail("The co-op folder cannot be written.");
#if !JUCE_WINDOWS
    chmod(file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR); // the token is a secret
#endif
    return juce::Result::ok();
}

juce::File newCopyFolder(const juce::File& parent, const juce::String& title)
{
    auto name = juce::File::createLegalFileName(title.trim()).trimCharactersAtStart(".").trim();
    if (name.isEmpty())
        name = "Session";
    auto folder = parent.getChildFile(name);
    for (int number = 2; folder.exists(); ++number)
        folder = parent.getChildFile(name + " " + juce::String(number));
    return folder;
}

// CoopSession ----------------------------------------------------------------------------

CoopSession::LinkFactory CoopSession::tlsLinks()
{
    return [](const CoopServer& where) -> std::unique_ptr<net::Link>
    { return std::make_unique<net::TlsClientLink>(where.host, where.port, where.fingerprint); };
}

CoopSession::CoopSession(Session& sessionToUse, LinkFactory links, Options sessionOptions)
    : session(sessionToUse), makeLink(std::move(links)), options(std::move(sessionOptions))
{
    if (!options.clock)
        options.clock = [] { return engine::midi::hostSeconds(); };
}

CoopSession::~CoopSession()
{
    stopCollaborating();
}

double CoopSession::now() const
{
    return options.clock();
}

bool CoopSession::isActive() const noexcept
{
    return client != nullptr && session.getCollaboration() == this;
}

void CoopSession::start(const CoopServer& serverToUse, const juce::File& copyFolder)
{
    server = serverToUse;
    folder = copyFolder;
    state = session.getProject().getState();
    auto clientOptions = options.client;
    clientOptions.appVersion = options.appVersion;
    client = std::make_unique<coop::Client>(state, folder, [this] { return makeLink(server); },
                                            clientOptions);
    membershipWritten = false;
    savedAt = now();
    session.setCollaboration(this);
}

void CoopSession::stopCollaborating()
{
    if (session.getCollaboration() == this)
        session.setCollaboration(nullptr);
}

juce::Result CoopSession::host(const CoopServerLink& link, const juce::String& name,
                               const juce::File& copyFolder)
{
    if (isActive())
        return juce::Result::fail("This copy is in a co-op session already.");
    if (session.isRecording())
        return juce::Result::fail("Stop recording first.");
    if (!copyFolder.createDirectory())
        return juce::Result::fail("The folder for the co-op copy cannot be made.");

    // Lengths learnt from the files first: in co-op every clip comes with its own.
    [[maybe_unused]] const bool loaded = session.waitForLoads(10'000);

    const auto original = session.getProjectFile();
    const auto title = original == juce::File() ? juce::String("Session")
                                                : original.getFileNameWithoutExtension();
    const auto copyFile = copyFolder.getChildFile(title + model::projectFileExtension);
    if (const auto saved = session.saveProject(copyFile); saved.failed())
        return saved;

    // Every file the copy refers to goes into its folder, where every participant finds
    // it: those from elsewhere are copied into "Audio/1" (the host's), once each.
    auto loadedCopy = model::loadProject(copyFile);
    if (!loadedCopy.succeeded())
        return juce::Result::fail(loadedCopy.error);
    auto& project = *loadedCopy.project;
    const auto audioFolder = copyFolder.getChildFile("Audio");
    const auto ownFolder = audioFolder.getChildFile("1");
    std::map<juce::String, juce::File> copies;
    const auto inCopy = [&](const juce::File& file) -> juce::File
    {
        if (file == juce::File() || file.isAChildOf(audioFolder))
            return file;
        auto& copy = copies[file.getFullPathName()];
        if (copy == juce::File())
        {
            ownFolder.createDirectory();
            copy = ownFolder.getNonexistentChildFile(file.getFileNameWithoutExtension(),
                                                     file.getFileExtension(), false);
            if (file.existsAsFile())
                file.copyFileTo(copy);
        }
        return copy;
    };

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
            if (auto clip = track.getClip(c); !clip.isMidi())
                clip.setAudioFile(inCopy(clip.getAudioFile()));
        const auto relinkEffect = [&](model::Effect effect)
        {
            for (const auto& [key, file] : effect.getFiles())
                effect.setFile(key, inCopy(file));
        };
        if (const auto instrument = track.getInstrument())
            relinkEffect(*instrument);
        for (int e = 0; e < track.getEffects().getNumEffects(); ++e)
            relinkEffect(track.getEffects().getEffect(e));
    }
    if (const auto saved = model::saveProject(project, copyFile); saved.failed())
        return saved;
    if (const auto opened = session.openProject(copyFile); opened.failed())
        return opened;

    participantName = name;
    start(link.server, copyFolder);
    addOwnFiles(state);
    client->host(link.hostKey, name);
    return juce::Result::ok();
}

void CoopSession::addOwnFiles(const juce::ValueTree& tree)
{
    for (int index = 0; index < tree.getNumProperties(); ++index)
        if (const auto property = tree.getPropertyName(index);
            model::isPathProperty(tree.getType(), property))
            if (const auto path = tree[property].toString(); path.isNotEmpty())
                if (const juce::File file(path); file.existsAsFile())
                    client->getFiles().addLocalFile(relativePathOf(file));
    for (const auto& child : tree)
        addOwnFiles(child);
}

juce::Result CoopSession::join(const CoopInvite& invite, const juce::String& name,
                               const juce::File& copyFolder)
{
    if (isActive())
        return juce::Result::fail("This copy is in a co-op session already.");
    if (session.isRecording())
        return juce::Result::fail("Stop recording first.");
    if (!copyFolder.createDirectory())
        return juce::Result::fail("The folder for the co-op copy cannot be made.");

    session.newProject();
    auto title = juce::File::createLegalFileName(invite.title.trim()).trimCharactersAtStart(".").trim();
    if (title.isEmpty())
        title = "Session";
    if (const auto saved = session.saveProject(copyFolder.getChildFile(title + model::projectFileExtension));
        saved.failed())
        return saved;

    participantName = name;
    start(invite.server, copyFolder);
    client->join(invite.code, name);
    return juce::Result::ok();
}

juce::Result CoopSession::rejoin(const juce::File& copyFolder)
{
    if (isActive())
        return juce::Result::fail("This copy is in a co-op session already.");
    const auto membership = readMembership(copyFolder);
    if (!membership)
        return juce::Result::fail("This folder is not the copy of a co-op session.");

    const auto projects = copyFolder.findChildFiles(juce::File::findFiles, false,
                                                    juce::String("*") + model::projectFileExtension);
    if (projects.isEmpty())
        return juce::Result::fail("The co-op copy has no project file.");
    if (const auto opened = session.openProject(projects[0]); opened.failed())
        return opened;

    participantName = membership->name;
    start(membership->server, copyFolder);
    client->rejoin(membership->sessionId, membership->participant, membership->token);
    return juce::Result::ok();
}

void CoopSession::leave()
{
    if (client != nullptr && isActive())
    {
        client->leave();
        [[maybe_unused]] const auto saved = saveCopy();
        folder.getChildFile(membershipFile).deleteFile();
    }
    stopCollaborating();
}

juce::Result CoopSession::saveCopy()
{
    if (client == nullptr)
        return juce::Result::fail("Not in a co-op session.");
    savedAt = now();
    savedSequence = client->getSequence();
    return session.saveProject(session.getProjectFile());
}

void CoopSession::update()
{
    if (client == nullptr)
        return;

    if (isActive() && session.getProject().getState() != state)
    {
        // Another project was opened: this one leaves the session's copy.
        notices.add("You left the co-op session: another project is open.");
        stopCollaborating();
        return;
    }

    client->update(now());

    if (client->getStatus() == coop::Client::Status::ended && isActive())
    {
        notices.add(client->getProblem() + " The copy stays, as an ordinary project.");
        [[maybe_unused]] const auto saved = saveCopy();
        // Removed or closed: opening the copy again opens an ordinary project. Only a copy
        // that needs an update first may come back.
        if (!client->canRejoinLater())
            folder.getChildFile(membershipFile).deleteFile();
        stopCollaborating();
    }

    for (const auto& path : client->getFiles().takeArrived())
        session.fileArrived(folder.getChildFile(path));
    for (const auto& notice : client->takeNotices())
        notices.add(notice.text);
    notices.addArray(client->getFiles().takeNotices());

    if (!isActive())
        return;

    if (!membershipWritten && client->getStatus() == coop::Client::Status::connected
        && client->getToken().isNotEmpty())
    {
        [[maybe_unused]] const auto written =
            writeMembership(folder, {server, client->getSessionId(), client->getParticipant(),
                                     client->getToken(), participantName});
        membershipWritten = true;
    }

    for (const auto& start : client->takeTransportStarts())
        startTransport(start);

    if (presence)
    {
        auto current = *presence;
        current.playing = session.getTransportState() == engine::TransportState::playing;
        current.playhead = session.getPlayhead();
        client->setPresence(current);
    }

    watchPlugins();

    if (now() - savedAt >= options.autosaveInterval
        && (client->getSequence() != savedSequence || session.hasUnsavedChanges()))
        [[maybe_unused]] const auto saved = saveCopy();
}

void CoopSession::watchPlugins()
{
    const auto unavailable = session.getUnavailablePlugins();
    for (const auto& plugin : unavailable)
        if (!unavailablePlugins.contains(plugin))
            notices.add("\"" + plugin
                        + "\" is not installed on this computer: its track plays without it here "
                          "(its settings are kept for the others).");
    unavailablePlugins = unavailable;
}

void CoopSession::startTransport(const coop::TransportStart& start)
{
    if (!following || session.isRecording())
        return;
    if (!start.play)
    {
        session.stop();
        return;
    }

    auto& engine = session.getEngine();
    if (const auto local = client->localTime(start.startTime))
        if (const auto sample = streamSampleHeardAt(engine.getStreamClock(), *local,
                                                    session.getDeviceOutputLatency(),
                                                    engine.getMaximumBlockSize()))
        {
            session.playAt(start.position, *sample);
            return;
        }

    // No clock to start on (no device times): at once.
    session.seek(start.position);
    session.play();
}

juce::String CoopSession::getTitle() const
{
    const auto file = session.getProjectFile();
    return file == juce::File() ? juce::String() : file.getFileNameWithoutExtension();
}

juce::String CoopSession::getInviteLink() const
{
    if (client == nullptr || !client->getInvite() || client->getInvite()->code.isEmpty())
        return {};
    return inviteLink({server, client->getInvite()->code, getTitle()});
}

std::optional<double> CoopSession::getInviteSecondsLeft() const
{
    if (client == nullptr || !client->getInvite() || client->getInvite()->code.isEmpty())
        return std::nullopt;
    if (const auto serverNow = client->serverTime(now()))
        return std::max(0.0, client->getInvite()->expiryTime - *serverNow);
    return std::nullopt;
}

std::optional<coop::FileSync::State> CoopSession::getFileState(const juce::File& file) const
{
    if (client == nullptr)
        return std::nullopt;
    return client->getFiles().stateOf(relativePathOf(file));
}

juce::String CoopSession::describeFile(const juce::File& file) const
{
    if (!isActive())
        return {};
    const auto fileState = getFileState(file);
    if (!fileState)
        return {};
    using Kind = coop::FileSync::State::Kind;
    switch (fileState->kind)
    {
    case Kind::incoming:
        return "Incoming";
    case Kind::downloading:
        return "Downloading "
               + juce::String(fileState->size > 0 ? static_cast<int>(100 * fileState->done / fileState->size) : 0)
               + " %";
    case Kind::failed:
        return "Not available";
    case Kind::present:
    case Kind::hashing:
    case Kind::uploading:
        break;
    }
    return {};
}

bool CoopSession::areFilesMoving() const
{
    return isActive() && !client->getFiles().isIdle();
}

std::vector<CoopPeer> CoopSession::getOthers() const
{
    std::vector<CoopPeer> others;
    if (!isActive())
        return others;
    const auto& presences = client->getPresences();
    for (const auto& participant : client->getParticipants())
    {
        if (participant.number == client->getParticipant())
            continue;
        CoopPeer peer{participant.number, participant.name, participant.colour, participant.connected,
                      participant.recording, {}};
        if (const auto found = presences.find(participant.number); found != presences.end())
            peer.presence = found->second;
        others.push_back(std::move(peer));
    }
    return others;
}

std::optional<CoopPeer> CoopSession::findOtherHolder(const model::EditScope& scope) const
{
    if (!isActive())
        return std::nullopt;
    const auto holder = client->holderOf(scope);
    if (holder == 0 || holder == client->getParticipant())
        return std::nullopt;
    for (auto& peer : getOthers())
        if (peer.number == holder)
            return peer;
    return CoopPeer{holder, "Someone", 0, false, false, {}};
}

std::uint64_t CoopSession::getRevision() const noexcept
{
    return client != nullptr ? client->getRevision() : 0;
}

juce::StringArray CoopSession::takeNotices()
{
    return std::exchange(notices, {});
}

void CoopSession::setPresence(coop::Presence newPresence)
{
    presence = std::move(newPresence);
}

void CoopSession::playForEveryone(model::Ticks position)
{
    if (isActive())
        client->requestTransport(true, position);
}

void CoopSession::stopForEveryone()
{
    if (isActive())
        client->requestTransport(false, session.getPlayhead());
}

juce::String CoopSession::relativePathOf(const juce::File& file) const
{
    return file.getRelativePathFrom(folder).replaceCharacter('\\', '/');
}

// Collaboration --------------------------------------------------------------------------

bool CoopSession::mayEdit(const std::vector<model::EditScope>& scopes, juce::String& why)
{
    if (!isActive())
        return true;

    for (const auto& scope : scopes)
    {
        if (client->mayEdit(scope))
            continue;

        const auto holder = client->holderOf(scope);
        if (holder != 0 && holder != client->getParticipant())
        {
            const auto* info = client->findParticipant(holder);
            juce::String what = "the project (tempo, metre, loop, tracks)";
            if (scope.kind == model::EditScope::Kind::master)
                what = "the master";
            else if (scope.kind == model::EditScope::Kind::track)
            {
                const int index = session.getProject().indexOfTrack(scope.id);
                what = index >= 0 ? "track \"" + session.getProject().getTrack(index).getName() + "\""
                                  : juce::String("that track");
            }
            else if (scope.kind == model::EditScope::Kind::bus)
            {
                const int index = session.getProject().indexOfBus(scope.id);
                what = index >= 0 ? "bus \"" + session.getProject().getBus(index).getName() + "\""
                                  : juce::String("that bus");
            }
            why = (info != nullptr ? info->name : juce::String("Someone")) + " is editing " + what + ".";
        }
        else if (client->getStatus() == coop::Client::Status::reconnecting)
            why = "The session is reconnecting: only what you were editing can change meanwhile.";
        else
            why = "This copy is not in step with the session yet: nothing can change until it is.";
        return false;
    }
    return true;
}

bool CoopSession::holds(const model::EditScope& scope) const
{
    return isActive() && client->getParticipant() != 0
           && client->holderOf(scope) == client->getParticipant();
}

void CoopSession::beginTransaction(const juce::String& name, bool undoable, bool continuesStep)
{
    if (!isActive())
        return;
    if (!undoable)
    {
        client->setTransaction(name, 0);
        return;
    }
    if (!continuesStep || currentStep == 0)
        currentStep = client->newUndoStep();
    client->setTransaction(name, currentStep);
}

void CoopSession::beginGesture()
{
    if (!isActive())
        return;
    currentStep = client->newUndoStep();
    client->setFlushInterval(0.1);
}

void CoopSession::endGesture()
{
    if (!isActive())
        return;
    client->flush();
    client->setFlushInterval(0.0);
}

bool CoopSession::undo()
{
    if (!isActive())
        return false;
    const bool undone = client->undo();
    for (const auto& notice : client->takeNotices())
        notices.add(notice.text);
    return undone;
}

bool CoopSession::redo()
{
    if (!isActive())
        return false;
    const bool redone = client->redo();
    for (const auto& notice : client->takeNotices())
        notices.add(notice.text);
    return redone;
}

bool CoopSession::canUndo() const
{
    return isActive() && client->canUndo();
}

bool CoopSession::canRedo() const
{
    return isActive() && client->canRedo();
}

juce::String CoopSession::getUndoName() const
{
    return isActive() ? client->getUndoName() : juce::String();
}

juce::String CoopSession::getRedoName() const
{
    return isActive() ? client->getRedoName() : juce::String();
}

juce::File CoopSession::getOwnFilesFolder() const
{
    const auto number = client != nullptr && client->getParticipant() != 0 ? client->getParticipant() : 1u;
    return folder.getChildFile("Audio").getChildFile(juce::String(number));
}

void CoopSession::addOwnFile(const juce::File& file)
{
    if (client != nullptr)
        client->getFiles().addLocalFile(relativePathOf(file));
}

bool CoopSession::isFileReady(const juce::File& file) const
{
    return client != nullptr && !client->getFiles().entriesFor({relativePathOf(file)}).empty();
}

void CoopSession::holdLocks(const std::vector<model::EditScope>& scopes)
{
    if (isActive())
        client->holdLocks(scopes);
}

void CoopSession::setRecording(bool recording)
{
    if (isActive())
        client->setRecording(recording);
}

} // namespace daw::controller
