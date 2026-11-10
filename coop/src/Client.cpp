#include "daw/coop/Client.h"

#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"
#include "daw/model/SharedProject.h"

#include <algorithm>

namespace daw::coop
{

namespace
{

const juce::Identifier trackType("Track");
const juce::Identifier busType("Bus");
const juce::Identifier idProperty("id");
const juce::Identifier nameProperty("name");

bool intersects(const std::vector<model::EditScope>& a, const std::vector<model::EditScope>& b)
{
    return std::any_of(a.begin(), a.end(),
                       [&b](const model::EditScope& scope)
                       { return std::find(b.begin(), b.end(), scope) != b.end(); });
}

// A participant's personal settings: the project's own and each track's, by its id.
struct PersonalSettings
{
    juce::NamedValueSet project;
    std::map<model::ObjectId, juce::NamedValueSet> tracks;
};

juce::NamedValueSet personalPropertiesOf(const juce::ValueTree& node)
{
    juce::NamedValueSet properties;
    for (int index = 0; index < node.getNumProperties(); ++index)
        if (const auto name = node.getPropertyName(index);
            model::isPersonalProperty(node.getType(), name))
            properties.set(name, node[name]);
    return properties;
}

PersonalSettings personalSettingsOf(const juce::ValueTree& project)
{
    PersonalSettings settings;
    settings.project = personalPropertiesOf(project);
    for (const auto& child : project)
        if (child.hasType(trackType))
            settings.tracks[static_cast<model::ObjectId>(child[idProperty])] =
                personalPropertiesOf(child);
    return settings;
}

void restore(juce::ValueTree& project, const PersonalSettings& settings)
{
    for (const auto& property : settings.project)
        project.setProperty(property.name, property.value, nullptr);
    for (auto child : project)
        if (child.hasType(trackType))
            if (const auto found = settings.tracks.find(static_cast<model::ObjectId>(child[idProperty]));
                found != settings.tracks.end())
                for (const auto& property : found->second)
                    child.setProperty(property.name, property.value, nullptr);
}

void collectPaths(const juce::ValueTree& tree, juce::StringArray& paths)
{
    for (int index = 0; index < tree.getNumProperties(); ++index)
        if (const auto name = tree.getPropertyName(index); model::isPathProperty(tree.getType(), name))
            if (const auto path = tree[name].toString(); path.isNotEmpty())
                paths.addIfNotAlreadyThere(path);
    for (const auto& child : tree)
        collectPaths(child, paths);
}

juce::StringArray pathsIn(const model::ChangeSet& sharedChanges)
{
    juce::StringArray paths;
    for (const auto& change : sharedChanges)
    {
        if (change.kind == model::ProjectChange::Kind::insertChild)
            collectPaths(change.child, paths);
        else if (change.kind == model::ProjectChange::Kind::setProperty && change.after
                 && model::isPathProperty(change.path.empty() ? juce::Identifier("Project")
                                                              : change.path.back().type,
                                          change.property)
                 && change.after->toString().isNotEmpty())
            paths.addIfNotAlreadyThere(change.after->toString());
    }
    return paths;
}

} // namespace

Client::Client(juce::ValueTree projectState, juce::File copyFolder, LinkFactory linkFactory,
               Options clientOptions)
    : state(std::move(projectState)),
      folder(std::move(copyFolder)),
      makeLink(std::move(linkFactory)),
      options(std::move(clientOptions)),
      recorder(state)
{
    recorder.setRecording(false);
    retryDelay = options.firstRetry;
}

Client::~Client() = default;

// Starting --------------------------------------------------------------------------------

void Client::host(const juce::String& key, const juce::String& participantName)
{
    request = Request::create;
    hostKey = key;
    name = participantName;
    status = Status::connecting;
    connectAt = now;
    changed();
}

void Client::join(const juce::String& code, const juce::String& participantName)
{
    request = Request::join;
    inviteCode = code;
    name = participantName;
    status = Status::connecting;
    connectAt = now;
    changed();
}

void Client::rejoin(const juce::String& id, ParticipantNumber number, const juce::String& secret)
{
    request = Request::rejoin;
    sessionId = id;
    participant = number;
    token = secret;
    status = Status::connecting;
    connectAt = now;
    changed();
}

void Client::leave()
{
    if (status == Status::idle || status == Status::ended)
        return;
    flush();
    send(Leave{});
    if (link != nullptr && welcomed)
        for (auto& frame : channel.takeFrames(now))
            link->send(channelFrame(frame));
    end("You left the session.");
}

void Client::connect()
{
    link = makeLink();
    welcomed = false;
    earlyFrames.clear();

    Hello hello;
    hello.protocolVersion = static_cast<std::uint32_t>(model::networkProtocolVersion);
    hello.formatVersion = static_cast<std::uint32_t>(model::currentFormatVersion);
    hello.appVersion = options.appVersion;
    hello.delivered = channel.getDelivered();
    switch (request)
    {
    case Request::create:
        hello.request = CreateSession{hostKey, name};
        break;
    case Request::join:
        hello.request = JoinSession{inviteCode, name};
        break;
    case Request::rejoin:
    case Request::none:
        hello.request = RejoinSession{sessionId, participant, token};
        break;
    }

    link->send(encodeFrame(hello));
    helloSentAt = now;
}

// The loop --------------------------------------------------------------------------------

void Client::update(double time)
{
    now = time;
    if (status == Status::idle || status == Status::ended)
        return;

    if (link == nullptr && now >= connectAt)
        connect();

    if (link != nullptr)
    {
        for (const auto& frame : link->receive())
        {
            handleFrame(frame);
            if (link == nullptr)
                break;
        }
        if (link != nullptr && link->getState() == net::Link::State::closed)
            linkLost(link->getProblem());
        else if (link != nullptr && !welcomed && now - helloSentAt > options.helloTimeout)
            linkLost("The server did not answer.");
    }

    if (synced)
        flush();

    if (link == nullptr || !welcomed)
        return;

    for (auto held = heldLocks.begin(); held != heldLocks.end();)
    {
        if (now - held->second > 2.0 * options.lockRefreshInterval)
            held = heldLocks.erase(held); // no longer asked for: it goes when unused
        else
            ++held;
    }

    if (presenceToSend && now - presenceSentAt >= options.presenceInterval)
    {
        send(PresenceUpdate{0, *presenceToSend});
        lastPresenceSent = *presenceToSend;
        presenceToSend.reset();
        presenceSentAt = now;
    }

    const auto pingInterval = clockSamples.size() < 5 ? 0.25 : options.pingInterval;
    if (now - pingSentAt >= pingInterval)
    {
        send(Ping{now});
        pingSentAt = now;
    }

    for (auto& frame : channel.takeFrames(now))
        link->send(channelFrame(frame));
}

void Client::handleFrame(const juce::MemoryBlock& frame)
{
    switch (frameKindOf(frame).value_or(FrameKind::hello))
    {
    case FrameKind::welcome:
        if (const auto welcome = decodeWelcome(frame))
        {
            handleWelcome(*welcome);
            const auto early = std::exchange(earlyFrames, {});
            for (const auto& waiting : early)
                handleFrame(waiting);
        }
        return;

    case FrameKind::refused:
    {
        const auto refused = decodeRefused(frame);
        if (refused && refused->reason == RefusalReason::notReady)
            return linkLost(describe(*refused));
        return end(refused ? describe(*refused) : juce::String("The server refused the connection."));
    }

    case FrameKind::channel:
        if (!welcomed)
        {
            // It overtook the welcome (the server sends it after): it waits.
            earlyFrames.push_back(frame);
            return;
        }
        if (!channel.handleFrame(channelPayload(frame), now))
            return linkLost("The server sent something this app does not understand.");
        for (auto& delivery : channel.takeDelivered())
        {
            if (const auto message = decodeMessage(delivery.payload))
                handleMessage(*message);
            if (link == nullptr || status == Status::ended)
                return;
        }
        return;

    case FrameKind::hello:
    case FrameKind::files:
        return;
    }
}

void Client::handleWelcome(const Welcome& welcome)
{
    welcomed = true;
    status = Status::connected;
    problem = {};
    retryDelay = options.firstRetry;
    sessionId = welcome.sessionId;
    participant = welcome.participant;
    if (welcome.token.isNotEmpty())
        token = welcome.token;
    model::setIdRange(state, model::idRangeOfParticipant(static_cast<int>(participant)));

    if (welcome.resumed)
        channel.resume(welcome.delivered);
    else
    {
        channel.reset();
        if (request == Request::create && !uploaded)
        {
            const auto project = model::sharedCopy(state, folder);
            ProjectUpload upload{project, {}};
            if (filesFor)
            {
                juce::StringArray paths;
                collectPaths(project, paths);
                upload.files = filesFor(paths);
            }
            send(upload);
            uploaded = true;
            synced = true;
            sequence = 0;
            [[maybe_unused]] const auto before = recorder.takeChanges();
            recorder.setRecording(true);
        }
        else
        {
            // Until the server's project arrives, nothing is in step.
            synced = false;
        }
    }

    request = Request::rejoin;
    changed();
}

void Client::linkLost(const juce::String& reason)
{
    link.reset();
    welcomed = false;
    earlyFrames.clear();
    problem = reason;

    if (status == Status::connected)
    {
        status = Status::reconnecting;
        disconnectedSince = now;
        connectAt = now + 0.5;
        retryDelay = options.firstRetry;
    }
    else
    {
        connectAt = now + retryDelay;
        retryDelay = std::min(retryDelay * 2.0, options.longestRetry);
    }
    changed();
}

void Client::end(const juce::String& reason)
{
    status = Status::ended;
    problem = reason;
    link.reset();
    welcomed = false;
    synced = false;
    recorder.setRecording(false);
    changed();
}

void Client::send(const Message& message)
{
    if (isUnreliable(message))
    {
        if (link != nullptr && welcomed)
            channel.sendUnreliable(encode(message));
        return;
    }
    channel.send(encode(message));
}

// Messages --------------------------------------------------------------------------------

void Client::handleMessage(const Message& message)
{
    if (const auto* snapshot = std::get_if<ProjectSnapshot>(&message))
        handleSnapshot(*snapshot);
    else if (const auto* accepted = std::get_if<AcceptedChange>(&message))
        handleAccepted(*accepted);
    else if (const auto* rejection = std::get_if<Rejection>(&message))
        handleRejection(*rejection);
    else if (const auto* list = std::get_if<ParticipantList>(&message))
    {
        participants = list->participants;
        for (auto presence = presences.begin(); presence != presences.end();)
            presence = findParticipant(presence->first) == nullptr ? presences.erase(presence)
                                                                   : std::next(presence);
        changed();
    }
    else if (const auto* lockState = std::get_if<LockState>(&message))
    {
        if (lockState->complete)
            locks.clear();
        for (const auto& [scope, holder] : lockState->locks)
        {
            if (holder == 0)
                locks.erase(scope);
            else
                locks[scope] = holder;
        }
        changed();
    }
    else if (const auto* update = std::get_if<PresenceUpdate>(&message))
    {
        presences[update->participant] = update->presence;
        changed();
    }
    else if (const auto* start = std::get_if<TransportStart>(&message))
        transportStarts.push_back(*start);
    else if (const auto* received = std::get_if<Invite>(&message))
    {
        invite = *received;
        changed();
    }
    else if (const auto* available = std::get_if<FileAvailable>(&message))
        availableFiles.push_back(available->hash);
    else if (const auto* ended = std::get_if<SessionEnded>(&message))
        end(ended->reason == SessionEnded::Reason::removed ? "The host removed you from this session."
                                                           : "The host closed this session.");
    else if (const auto* pong = std::get_if<Pong>(&message))
    {
        const auto roundTrip = now - pong->clientTime;
        if (roundTrip >= 0.0)
        {
            clockSamples.push_back({roundTrip, pong->serverTime - (pong->clientTime + now) * 0.5});
            while (clockSamples.size() > 16)
                clockSamples.pop_front();
        }
    }
}

void Client::handleSnapshot(const ProjectSnapshot& snapshot)
{
    const auto personal = personalSettingsOf(state);
    {
        const model::ChangeRecorder::Pause pause(recorder);
        auto fresh = model::localCopy(snapshot.project, folder);
        restore(fresh, personal);
        state.copyPropertiesAndChildrenFrom(fresh, nullptr);
    }
    sequence = snapshot.sequence;
    synced = true;
    recorder.setRecording(true);

    // Transactions the server dealt with are in the snapshot or were refused; the others go
    // again on top of it.
    while (!pending.empty() && pending.front().clientSequence <= snapshot.lastTransaction)
        pending.pop_front();
    for (auto transaction = pending.begin(); transaction != pending.end();)
    {
        if (applyToCopy(transaction->changes))
        {
            transaction->applied = true;
            sendPending(*transaction);
            ++transaction;
        }
        else
        {
            notify("Your change \"" + transaction->name + "\" no longer fits the session's project.");
            transaction = pending.erase(transaction);
        }
    }

    for (const auto& file : snapshot.files)
        newFiles.push_back(file);
    changed();
}

void Client::handleAccepted(const AcceptedChange& change)
{
    if (!synced || change.sequence <= sequence)
        return;
    if (change.sequence != sequence + 1)
    {
        // Out of step: ask for the whole project.
        synced = false;
        send(SnapshotRequest{});
        return;
    }
    sequence = change.sequence;
    for (const auto& file : change.files)
        newFiles.push_back(file);

    if (change.author == participant)
    {
        const auto found = std::find_if(pending.begin(), pending.end(),
                                        [&change](const Pending& transaction)
                                        { return transaction.clientSequence == change.clientSequence; });
        if (found != pending.end())
        {
            const auto index = static_cast<std::size_t>(std::distance(pending.begin(), found));
            if (index == 0 && found->applied)
            {
                pending.pop_front();
                return;
            }
            // The server applied what the copy did not, or after others still waiting.
            const auto local = model::localChanges(change.changes, folder);
            takeBackPending(0);
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
            if (!applyToCopy(local))
            {
                synced = false;
                send(SnapshotRequest{});
                return;
            }
            applyPendingAgain(0);
            changed();
            return;
        }
    }

    const auto local = model::localChanges(change.changes, folder);
    const auto scopes = model::scopesOf(local);
    const bool touchesWaiting = std::any_of(pending.begin(), pending.end(),
                                            [&scopes](const Pending& transaction)
                                            { return transaction.applied && intersects(transaction.scopes, scopes); });
    if (touchesWaiting)
        takeBackPending(0);
    const bool fits = applyToCopy(local);
    if (touchesWaiting)
        applyPendingAgain(0);
    if (!fits)
    {
        synced = false;
        send(SnapshotRequest{});
    }
    changed();
}

void Client::handleRejection(const Rejection& rejection)
{
    const auto found = std::find_if(pending.begin(), pending.end(),
                                    [&rejection](const Pending& transaction)
                                    { return transaction.clientSequence == rejection.clientSequence; });
    if (found == pending.end())
        return;

    const auto index = static_cast<std::size_t>(std::distance(pending.begin(), found));
    const auto refused = *found;
    takeBackPending(index);
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
    applyPendingAgain(index);

    if (rejection.reason == RejectionReason::locked && rejection.holder != 0)
        for (const auto& scope : refused.scopes)
            if (const auto holder = holderOf(scope); holder == 0 || holder == participant)
                locks[scope] = rejection.holder;

    // Undo steps lose what the server refused.
    const auto forget = [&refused](std::vector<UndoStep>& steps)
    {
        steps.erase(std::remove_if(steps.begin(), steps.end(),
                                   [&refused](const UndoStep& step) { return step.id == refused.undoStep; }),
                    steps.end());
    };
    if (refused.undoStep != 0)
    {
        if (refused.kind == Pending::Kind::undo)
            forget(redoHistory);
        else
            forget(history);
    }

    switch (rejection.reason)
    {
    case RejectionReason::locked:
        notify(nameOf(rejection.holder) + " is editing "
               + (refused.scopes.empty() ? juce::String("it") : describeScope(refused.scopes.front()))
               + ": \"" + refused.name + "\" was not kept.");
        break;
    case RejectionReason::recording:
        notify(nameOf(rejection.holder)
               + " is recording: the tempo, the metre and effect chains stay as they are until "
                 "they stop. \""
               + refused.name + "\" was not kept.");
        break;
    case RejectionReason::conflict:
        notify("\"" + refused.name + "\" no longer fits: someone changed the same thing first.");
        break;
    case RejectionReason::malformed:
    case RejectionReason::readOnly:
        notify("The server did not accept \"" + refused.name + "\".");
        break;
    }
    changed();
}

// Applying ---------------------------------------------------------------------------------

bool Client::applyToCopy(const model::ChangeSet& changes)
{
    const model::ChangeRecorder::Pause pause(recorder);
    return model::applyChanges(state, changes).wasOk();
}

void Client::takeBackPending(std::size_t from)
{
    for (auto index = pending.size(); index-- > from;)
        if (pending[index].applied && !applyToCopy(model::inverted(pending[index].changes)))
        {
            // Cannot happen while the copy only changes through here: start again from the
            // server's project.
            jassertfalse;
            pending[index].applied = false;
            synced = false;
            send(SnapshotRequest{});
        }
}

void Client::applyPendingAgain(std::size_t from)
{
    for (auto index = from; index < pending.size(); ++index)
        if (pending[index].applied)
            pending[index].applied = applyToCopy(pending[index].changes);
}

// Local edits ------------------------------------------------------------------------------

void Client::setTransaction(const juce::String& transactionNameToUse, std::uint64_t undoStep)
{
    flush();
    transactionName = transactionNameToUse;
    transactionStep = undoStep;
}

void Client::flush()
{
    if (!recorder.hasChanges())
        return;
    auto changes = recorder.takeChanges();

    const auto scopes = model::scopesOf(changes);
    for (const auto& scope : scopes)
    {
        if (mayEdit(scope))
            continue;
        [[maybe_unused]] const bool undone = applyToCopy(model::inverted(changes));
        jassert(undone);
        const auto holder = holderOf(scope);
        notify(holder != 0 && holder != participant
                   ? nameOf(holder) + " is editing " + describeScope(scope) + "."
                   : juce::String("Nothing can change while the session is not connected."));
        changed();
        return;
    }

    if (transactionStep != 0)
    {
        if (!history.empty() && history.back().id == transactionStep)
            history.back().changes.insert(history.back().changes.end(), changes.begin(), changes.end());
        else
        {
            history.push_back({transactionStep, transactionName, changes});
            redoHistory.clear();
        }
    }

    Pending transaction;
    transaction.clientSequence = ++lastClientSequence;
    transaction.name = transactionName;
    transaction.undoStep = transactionStep;
    transaction.changes = std::move(changes);
    transaction.scopes = scopes;
    submit(std::move(transaction));
}

void Client::submit(Pending transaction)
{
    for (const auto& scope : transaction.scopes)
        if (holderOf(scope) == 0)
            locks[scope] = participant; // the server says so once it has it
    pending.push_back(std::move(transaction));
    sendPending(pending.back());
    changed();
}

void Client::sendPending(const Pending& transaction)
{
    Transaction message;
    message.clientSequence = transaction.clientSequence;
    message.name = transaction.name;
    message.undoStep = transaction.undoStep;
    message.changes = model::sharedChanges(transaction.changes, folder);
    if (filesFor)
        if (const auto paths = pathsIn(message.changes); !paths.isEmpty())
            message.files = filesFor(paths);
    send(message);
}

bool Client::mayEdit(const model::EditScope& scope) const
{
    if (!synced)
        return false;
    const auto holder = holderOf(scope);
    if (holder != 0 && holder != participant)
        return false;
    if (status == Status::connected)
        return true;
    return status == Status::reconnecting && now - disconnectedSince < options.readOnlyAfter
           && holder == participant;
}

ParticipantNumber Client::holderOf(const model::EditScope& scope) const
{
    const auto found = locks.find(scope);
    return found != locks.end() ? found->second : 0;
}

void Client::holdLocks(const std::vector<model::EditScope>& scopes)
{
    for (const auto& scope : scopes)
    {
        const auto asked = heldLocks.find(scope);
        if (asked != heldLocks.end() && now - asked->second < options.lockRefreshInterval)
            continue;
        heldLocks[scope] = now;
        if (holderOf(scope) == 0)
            locks[scope] = participant;
        send(LockRequest{scope, true});
    }
}

void Client::releaseLock(const model::EditScope& scope)
{
    heldLocks.erase(scope);
    if (holderOf(scope) == participant)
        locks.erase(scope);
    send(LockRequest{scope, false});
}

// Personal undo ----------------------------------------------------------------------------

juce::String Client::getUndoName() const
{
    return history.empty() ? juce::String() : history.back().name;
}

juce::String Client::getRedoName() const
{
    return redoHistory.empty() ? juce::String() : redoHistory.back().name;
}

bool Client::undo()
{
    flush();
    if (history.empty())
        return false;

    const auto step = history.back();
    const auto inverse = model::inverted(step.changes);
    const auto scopes = model::scopesOf(inverse);
    for (const auto& scope : scopes)
        if (!mayEdit(scope))
        {
            const auto holder = holderOf(scope);
            notify("Cannot undo \"" + step.name + "\" now: "
                   + (holder != 0 && holder != participant
                          ? nameOf(holder) + " is editing " + describeScope(scope) + "."
                          : juce::String("the session is not connected.")));
            return false;
        }

    history.pop_back();
    if (!applyToCopy(inverse))
    {
        notify("Cannot undo \"" + step.name + "\": someone changed it since.");
        changed();
        return false;
    }
    redoHistory.push_back(step);

    Pending transaction;
    transaction.clientSequence = ++lastClientSequence;
    transaction.name = "Undo " + step.name;
    transaction.undoStep = step.id;
    transaction.changes = inverse;
    transaction.scopes = scopes;
    transaction.kind = Pending::Kind::undo;
    submit(std::move(transaction));
    return true;
}

bool Client::redo()
{
    flush();
    if (redoHistory.empty())
        return false;

    const auto step = redoHistory.back();
    const auto scopes = model::scopesOf(step.changes);
    for (const auto& scope : scopes)
        if (!mayEdit(scope))
        {
            const auto holder = holderOf(scope);
            notify("Cannot redo \"" + step.name + "\" now: "
                   + (holder != 0 && holder != participant
                          ? nameOf(holder) + " is editing " + describeScope(scope) + "."
                          : juce::String("the session is not connected.")));
            return false;
        }

    redoHistory.pop_back();
    if (!applyToCopy(step.changes))
    {
        notify("Cannot redo \"" + step.name + "\": someone changed it since.");
        changed();
        return false;
    }
    history.push_back(step);

    Pending transaction;
    transaction.clientSequence = ++lastClientSequence;
    transaction.name = "Redo " + step.name;
    transaction.undoStep = step.id;
    transaction.changes = step.changes;
    transaction.scopes = scopes;
    transaction.kind = Pending::Kind::redo;
    submit(std::move(transaction));
    return true;
}

// The session ------------------------------------------------------------------------------

bool Client::isHost() const
{
    const auto* self = findParticipant(participant);
    return self != nullptr && self->host;
}

const ParticipantInfo* Client::findParticipant(ParticipantNumber number) const
{
    const auto found = std::find_if(participants.begin(), participants.end(),
                                    [number](const ParticipantInfo& info) { return info.number == number; });
    return found != participants.end() ? &*found : nullptr;
}

std::vector<Client::Notice> Client::takeNotices()
{
    return std::exchange(notices, {});
}

std::vector<FileEntry> Client::takeNewFiles()
{
    return std::exchange(newFiles, {});
}

std::vector<juce::MemoryBlock> Client::takeAvailableFiles()
{
    return std::exchange(availableFiles, {});
}

void Client::sendHostCommand(HostCommand command)
{
    send(command);
}

void Client::setPresence(const Presence& presence)
{
    if (presence == lastPresenceSent && !presenceToSend)
        return;
    presenceToSend = presence;
}

void Client::setRecording(bool recording)
{
    send(RecordingState{recording});
}

void Client::requestTransport(bool play, model::Ticks position)
{
    send(TransportRequest{play, position});
}

std::vector<TransportStart> Client::takeTransportStarts()
{
    return std::exchange(transportStarts, {});
}

std::optional<double> Client::serverTime(double localTime) const
{
    if (clockSamples.empty())
        return std::nullopt;
    const auto best = std::min_element(clockSamples.begin(), clockSamples.end(),
                                       [](const ClockSample& a, const ClockSample& b)
                                       { return a.roundTrip < b.roundTrip; });
    return localTime + best->offset;
}

std::optional<double> Client::clockUncertainty() const
{
    if (clockSamples.empty())
        return std::nullopt;
    const auto best = std::min_element(clockSamples.begin(), clockSamples.end(),
                                       [](const ClockSample& a, const ClockSample& b)
                                       { return a.roundTrip < b.roundTrip; });
    return best->roundTrip * 0.5;
}

void Client::notify(const juce::String& text)
{
    notices.push_back({text});
}

juce::String Client::nameOf(ParticipantNumber number) const
{
    const auto* info = findParticipant(number);
    return info != nullptr ? info->name : juce::String("Someone");
}

juce::String Client::describeScope(const model::EditScope& scope) const
{
    switch (scope.kind)
    {
    case model::EditScope::Kind::project:
        return "the project (tempo, metre, loop, tracks)";
    case model::EditScope::Kind::master:
        return "the master";
    case model::EditScope::Kind::track:
    case model::EditScope::Kind::bus:
    {
        model::PathStep step;
        step.key = model::PathStep::Key::id;
        step.id = scope.id;
        model::NodePath path;
        if (scope.kind == model::EditScope::Kind::bus)
        {
            model::PathStep buses;
            buses.type = juce::Identifier("Buses");
            path.push_back(buses);
            step.type = busType;
        }
        else
            step.type = trackType;
        path.push_back(step);
        const auto node = model::findNode(state, path);
        const auto kind = scope.kind == model::EditScope::Kind::bus ? "bus" : "track";
        return node.isValid() ? juce::String(kind) + " \"" + node[nameProperty].toString() + "\""
                              : juce::String("that ") + kind;
    }
    }
    return "it";
}

} // namespace daw::coop
