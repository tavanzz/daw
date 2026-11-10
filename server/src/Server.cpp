#include "daw/server/Server.h"

#include "SessionStore.h"
#include "ServerState.h"

#include "daw/coop/Wire.h"
#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"
#include "daw/model/SharedProject.h"
#include "daw/net/ReliableChannel.h"
#include "daw/net/Tls.h"

#include <algorithm>
#include <map>
#include <set>

namespace daw::server
{

namespace
{

constexpr const char* codeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr int maximumNameLength = 40;
constexpr double closingDelay = 1.0; // for a last message to go out before a link closes

juce::String randomHex(int bytes)
{
    juce::MemoryBlock block(static_cast<std::size_t>(bytes));
    [[maybe_unused]] const bool ok = net::secureRandom(block.getData(), block.getSize());
    jassert(ok);
    return juce::String::toHexString(block.getData(), bytes, 0);
}

juce::String newInviteCode()
{
    std::uint8_t bytes[10];
    [[maybe_unused]] const bool ok = net::secureRandom(bytes, sizeof(bytes));
    jassert(ok);
    juce::String code;
    for (std::size_t index = 0; index < sizeof(bytes); ++index)
    {
        if (index == 5)
            code << '-';
        code << codeAlphabet[bytes[index] % 32]; // 256 is a multiple of 32: no bias
    }
    return code;
}

bool sameSecret(const juce::String& a, const juce::String& b)
{
    // Compared in constant time, so that timing says nothing about how much matched.
    const auto x = a.toUTF8();
    const auto y = b.toUTF8();
    const auto length = std::max(x.sizeInBytes(), y.sizeInBytes());
    unsigned difference = x.sizeInBytes() == y.sizeInBytes() ? 0u : 1u;
    for (std::size_t index = 0; index < length; ++index)
    {
        const auto p = index < x.sizeInBytes() ? static_cast<unsigned char>(x.getAddress()[index]) : 0u;
        const auto q = index < y.sizeInBytes() ? static_cast<unsigned char>(y.getAddress()[index]) : 0u;
        difference |= p ^ q;
    }
    return difference == 0;
}

juce::String cleanName(const juce::String& name)
{
    juce::String clean;
    for (const auto character : name.trim())
        if (character >= 0x20 && character != 0x7f)
            clean << juce::String::charToString(character);
    clean = clean.substring(0, maximumNameLength).trim();
    return clean.isEmpty() ? juce::String("Guest") : clean;
}

juce::String shortId(const juce::String& sessionId)
{
    return sessionId.substring(0, 8);
}

bool pathsAreSafe(const juce::ValueTree& tree)
{
    for (int index = 0; index < tree.getNumProperties(); ++index)
    {
        const auto name = tree.getPropertyName(index);
        if (model::isPathProperty(tree.getType(), name))
        {
            const auto value = tree[name];
            if (!value.isString()
                || (value.toString().isNotEmpty() && !coop::isSafeSharedPath(value.toString())))
                return false;
        }
    }
    return std::all_of(tree.begin(), tree.end(),
                       [](const juce::ValueTree& child) { return pathsAreSafe(child); });
}

bool pathsAreSafe(const model::ProjectChange& change)
{
    switch (change.kind)
    {
    case model::ProjectChange::Kind::setProperty:
    {
        const auto type = change.path.empty() ? juce::Identifier("Project") : change.path.back().type;
        if (!model::isPathProperty(type, change.property) || !change.after)
            return true;
        return change.after->isString()
               && (change.after->toString().isEmpty()
                   || coop::isSafeSharedPath(change.after->toString()));
    }
    case model::ProjectChange::Kind::insertChild:
        return pathsAreSafe(change.child);
    case model::ProjectChange::Kind::removeChild:
    case model::ProjectChange::Kind::moveChild:
        return true;
    }
    return true;
}

bool containsType(const juce::ValueTree& tree, const juce::Identifier& type)
{
    if (tree.hasType(type))
        return true;
    return std::any_of(tree.begin(), tree.end(),
                       [&type](const juce::ValueTree& child) { return containsType(child, type); });
}

// Changes nobody may make while someone records: the tempo and the metre (the recording
// would no longer match its beats) and the structure of effect chains (the latency).
bool changesWhatRecordingNeeds(const model::ChangeSet& changes)
{
    static const juce::Identifier tempo("tempo"), numerator("timeSignatureNumerator"),
        denominator("timeSignatureDenominator"), effect("Effect"), effects("Effects"),
        instrument("Instrument");

    for (const auto& change : changes)
    {
        switch (change.kind)
        {
        case model::ProjectChange::Kind::setProperty:
            if (change.path.empty()
                && (change.property == tempo || change.property == numerator
                    || change.property == denominator))
                return true;
            break;
        case model::ProjectChange::Kind::insertChild:
        case model::ProjectChange::Kind::removeChild:
            if (containsType(change.child, effect))
                return true;
            break;
        case model::ProjectChange::Kind::moveChild:
            if (!change.path.empty()
                && (change.path.back().type == effects || change.path.back().type == instrument))
                return true;
            break;
        }
    }
    return false;
}

bool scopeExists(const juce::ValueTree& project, const model::EditScope& scope)
{
    model::PathStep step;
    step.key = model::PathStep::Key::id;
    step.id = scope.id;

    switch (scope.kind)
    {
    case model::EditScope::Kind::project:
    case model::EditScope::Kind::master:
        return true;
    case model::EditScope::Kind::track:
        step.type = juce::Identifier("Track");
        return model::findNode(project, {step}).isValid();
    case model::EditScope::Kind::bus:
    {
        model::PathStep buses;
        buses.type = juce::Identifier("Buses");
        step.type = juce::Identifier("Bus");
        return model::findNode(project, {buses, step}).isValid();
    }
    }
    return false;
}

} // namespace

juce::String normaliseInviteCode(const juce::String& code)
{
    juce::String clean;
    for (const auto character : code.toUpperCase())
    {
        if (character == '-' || character == ' ')
            continue;
        if (character == 'O')
            clean << '0';
        else if (character == 'I' || character == 'L')
            clean << '1';
        else
            clean << juce::String::charToString(character);
    }
    return clean;
}

// Impl ------------------------------------------------------------------------------------

struct Server::Impl
{
    explicit Impl(ServerOptions serverOptions)
        : options(std::move(serverOptions))
    {
        if (options.dataFolder != juce::File())
        {
            store = std::make_unique<SessionStore>(options.dataFolder);
            for (auto& session : store->loadAll(log()))
            {
                const auto id = session.id;
                sessions.emplace(id, std::move(session));
            }
            closedSessions = store->loadClosedSessions();
            if (!sessions.empty())
                write("loaded " + juce::String(static_cast<int>(sessions.size())) + " sessions");
        }
    }

    std::function<void(const juce::String&)> log() const
    {
        return [this](const juce::String& line) { write(line); };
    }

    void write(const juce::String& line) const
    {
        if (options.log)
            options.log(line);
    }

    // Connections --------------------------------------------------------------------

    void addConnection(std::unique_ptr<net::Link> link, const juce::String& address, double time)
    {
        now = time;
        Connection connection;
        connection.link = std::move(link);
        connection.address = address;
        connection.openedAt = now;
        connections.emplace(nextConnection++, std::move(connection));
    }

    void closeConnection(std::uint64_t id, bool afterDelay)
    {
        const auto found = connections.find(id);
        if (found == connections.end())
            return;
        auto& connection = found->second;
        if (!connection.closing)
        {
            connection.closing = true;
            connection.closeAt = afterDelay ? now + closingDelay : now;
        }
        detach(connection);
    }

    // The participant no longer has this connection.
    void detach(Connection& connection)
    {
        if (connection.session.isEmpty())
            return;
        const auto session = sessions.find(connection.session);
        connection.session = {};
        if (session == sessions.end())
            return;
        const auto participant = session->second.participants.find(connection.participant);
        if (participant != session->second.participants.end()
            && participant->second.connection != 0)
        {
            auto& who = participant->second;
            who.connection = 0;
            who.disconnectedAt = now;
            broadcastParticipants(session->second);
        }
    }

    void refuse(std::uint64_t id, coop::RefusalReason reason, const juce::String& detail = {})
    {
        auto& connection = connections.at(id);
        coop::Refused refused;
        refused.reason = reason;
        refused.serverProtocol = static_cast<std::uint32_t>(model::networkProtocolVersion);
        refused.serverFormat = static_cast<std::uint32_t>(model::currentFormatVersion);
        refused.detail = detail;
        connection.link->send(coop::encodeFrame(refused));
        write("connection " + juce::String(id) + " refused: " + detail);
        closeConnection(id, true);
    }

    bool isBlocked(const juce::String& address)
    {
        const auto blocked = blockedUntil.find(address);
        return blocked != blockedUntil.end() && blocked->second > now;
    }

    void failedAttempt(const juce::String& address)
    {
        auto& times = failures[address];
        times.push_back(now);
        times.erase(std::remove_if(times.begin(), times.end(),
                                   [this](double time) { return now - time > options.failedAttemptsWindow; }),
                    times.end());
        if (static_cast<int>(times.size()) >= options.failedAttemptsAllowed)
        {
            blockedUntil[address] = now + options.blockDuration;
            times.clear();
            write("an address is blocked after too many failed attempts");
        }
    }

    // Frames ---------------------------------------------------------------------------

    void handleFrame(std::uint64_t id, const juce::MemoryBlock& frame)
    {
        auto& connection = connections.at(id);
        if (connection.closing)
            return;

        const auto kind = coop::frameKindOf(frame);
        if (connection.session.isEmpty())
        {
            if (kind != coop::FrameKind::hello)
                refuse(id, coop::RefusalReason::malformed, "not a hello");
            else
                handleHello(id, frame);
            return;
        }

        if (kind != coop::FrameKind::channel)
        {
            closeConnection(id, false);
            return;
        }

        auto& session = sessions.at(connection.session);
        auto& participant = session.participants.at(connection.participant);
        if (!participant.channel.handleFrame(coop::channelPayload(frame), now))
        {
            write("session " + shortId(session.id) + ": a malformed frame from participant "
                  + juce::String(participant.number));
            closeConnection(id, false);
            return;
        }

        for (auto& delivery : participant.channel.takeDelivered())
        {
            const auto message = coop::decodeMessage(delivery.payload);
            if (!message)
            {
                write("session " + shortId(session.id) + ": a malformed message from participant "
                      + juce::String(participant.number));
                closeConnection(id, false);
                return;
            }
            handleMessage(session, participant, *message);
            if (connection.closing || connection.session.isEmpty())
                return;
        }
    }

    void handleHello(std::uint64_t id, const juce::MemoryBlock& frame)
    {
        const auto versions = coop::readHelloVersions(frame);
        if (!versions)
            return refuse(id, coop::RefusalReason::malformed, "not a hello");
        if (!model::isCompatibleNetworkProtocol(static_cast<int>(versions->protocolVersion)))
            return refuse(id, coop::RefusalReason::incompatibleProtocol,
                          "protocol " + juce::String(versions->protocolVersion));
        if (versions->formatVersion != static_cast<std::uint32_t>(model::currentFormatVersion))
            return refuse(id, coop::RefusalReason::incompatibleProjectFormat,
                          "project format " + juce::String(versions->formatVersion));

        const auto hello = coop::decodeHello(frame);
        if (!hello)
            return refuse(id, coop::RefusalReason::malformed, "malformed hello");

        const auto address = connections.at(id).address;
        if (isBlocked(address))
            return refuse(id, coop::RefusalReason::tooManyAttempts, "blocked address");

        if (const auto* create = std::get_if<coop::CreateSession>(&hello->request))
            createSession(id, address, *create);
        else if (const auto* join = std::get_if<coop::JoinSession>(&hello->request))
            joinSession(id, address, *join);
        else if (const auto* rejoin = std::get_if<coop::RejoinSession>(&hello->request))
            rejoinSession(id, address, *rejoin, hello->delivered);
    }

    void createSession(std::uint64_t id, const juce::String& address,
                       const coop::CreateSession& request)
    {
        const bool keyMatches = std::any_of(options.hostKeys.begin(), options.hostKeys.end(),
                                            [&request](const juce::String& key)
                                            { return key.isNotEmpty() && sameSecret(key, request.hostKey); });
        if (!keyMatches)
        {
            failedAttempt(address);
            return refuse(id, coop::RefusalReason::badHostKey, "wrong host key");
        }
        if (static_cast<int>(sessions.size()) >= options.maximumSessions)
            return refuse(id, coop::RefusalReason::serverFull, "too many sessions");

        Session session;
        session.id = randomHex(16);
        session.inviteCode = newInviteCode();
        session.inviteExpiry = now + options.inviteLifetime;
        session.lastActive = now;
        session.dirty = true;

        const auto token = randomHex(16);
        auto& host = session.participants[1];
        host.number = 1;
        host.name = cleanName(request.name);
        host.host = true;
        host.tokenHash = SessionStore::hashOf(token);
        host.channelValid = true;

        const auto sessionId = session.id;
        auto& stored = sessions.emplace(sessionId, std::move(session)).first->second;
        write("session " + shortId(sessionId) + " created");

        welcome(id, stored, stored.participants.at(1), token, false);
        saveNow(stored);
        auto& owner = stored.participants.at(1);
        send(owner, coop::Invite{stored.inviteCode, stored.inviteExpiry});
        broadcastParticipants(stored);
        send(owner, lockState(stored));
    }

    void joinSession(std::uint64_t id, const juce::String& address, const coop::JoinSession& request)
    {
        const auto code = normaliseInviteCode(request.inviteCode);
        Session* found = nullptr;
        for (auto& [sessionId, session] : sessions)
            if (code.isNotEmpty() && sameSecret(normaliseInviteCode(session.inviteCode), code))
                found = &session;

        if (found == nullptr)
        {
            failedAttempt(address);
            return refuse(id, coop::RefusalReason::badInvite, "unknown invite code");
        }
        auto& session = *found;
        if (now > session.inviteExpiry)
            return refuse(id, coop::RefusalReason::expiredInvite, "expired invite");
        if (!session.ready)
            return refuse(id, coop::RefusalReason::notReady, "session not ready");
        if (countPresent(session) >= options.maximumParticipants)
            return refuse(id, coop::RefusalReason::sessionFull, "session full");

        coop::ParticipantNumber number = 1;
        for (const auto& [existing, participant] : session.participants)
            number = std::max(number, existing + 1);

        const auto token = randomHex(16);
        auto& participant = session.participants[number];
        participant.number = number;
        participant.name = cleanName(request.name);
        participant.colour = freeColour(session);
        participant.tokenHash = SessionStore::hashOf(token);
        participant.channelValid = true;
        session.dirty = true;
        write("session " + shortId(session.id) + ": participant " + juce::String(number)
              + " joined (" + juce::String(countPresent(session)) + " present)");

        welcome(id, session, participant, token, false);
        saveNow(session);
        sendSnapshot(session, participant);
        broadcastParticipants(session);
        send(participant, lockState(session));
    }

    void rejoinSession(std::uint64_t id, const juce::String& address,
                       const coop::RejoinSession& request, std::uint64_t delivered)
    {
        const auto found = sessions.find(request.sessionId);
        if (found == sessions.end())
        {
            if (closedSessions.count(request.sessionId) > 0)
                return refuse(id, coop::RefusalReason::sessionClosed, "session closed");
            failedAttempt(address);
            return refuse(id, coop::RefusalReason::badToken, "unknown session");
        }

        auto& session = found->second;
        const auto participantFound = session.participants.find(request.participant);
        if (participantFound == session.participants.end()
            || !sameSecret(participantFound->second.tokenHash, SessionStore::hashOf(request.token)))
        {
            failedAttempt(address);
            return refuse(id, coop::RefusalReason::badToken, "wrong token");
        }

        auto& participant = participantFound->second;
        if (participant.removed)
            return refuse(id, coop::RefusalReason::removed, "removed");
        if (participant.gone)
            return refuse(id, coop::RefusalReason::badToken, "left");

        if (participant.connection != 0)
            closeConnection(participant.connection, false); // an old connection, not dead yet

        const bool resumed = participant.channelValid && delivered <= participant.channel.getSent();
        if (resumed)
            participant.channel.resume(delivered);
        else
        {
            participant.channel.reset();
            participant.channelValid = true;
        }

        write("session " + shortId(session.id) + ": participant "
              + juce::String(participant.number) + (resumed ? " is back" : " is back from far"));
        welcome(id, session, participant, {}, resumed);
        if (!resumed)
        {
            sendSnapshot(session, participant);
            send(participant, lockState(session));
        }
        broadcastParticipants(session);
    }

    void welcome(std::uint64_t id, Session& session, Participant& participant,
                 const juce::String& token, bool resumed)
    {
        auto& connection = connections.at(id);
        connection.session = session.id;
        connection.participant = participant.number;
        participant.connection = id;
        participant.disconnectedAt = 0.0;
        session.lastActive = now;

        coop::Welcome message;
        message.sessionId = session.id;
        message.participant = participant.number;
        message.token = token;
        message.resumed = resumed;
        message.delivered = resumed ? participant.channel.getDelivered() : 0;
        message.lastTransaction = participant.lastTransaction;
        message.serverTime = now;
        connection.link->send(coop::encodeFrame(message));
    }

    // Messages --------------------------------------------------------------------------

    void send(Participant& participant, const coop::Message& message)
    {
        if (participant.gone && !std::holds_alternative<coop::SessionEnded>(message))
            return;

        if (coop::isUnreliable(message))
        {
            if (participant.connection != 0)
                participant.channel.sendUnreliable(coop::encode(message));
            return;
        }

        participant.channel.send(coop::encode(message));
        if (participant.channel.isOverwhelmed())
        {
            // Away too long, or not reading: a snapshot when they come back.
            participant.channel.reset();
            participant.channelValid = false;
            if (participant.connection != 0)
                closeConnection(participant.connection, false);
        }
    }

    void broadcast(Session& session, const coop::Message& message)
    {
        for (auto& [number, participant] : session.participants)
            send(participant, message);
    }

    void sendSnapshot(Session& session, Participant& participant)
    {
        coop::ProjectSnapshot snapshot;
        snapshot.sequence = session.sequence;
        snapshot.project = session.project;
        snapshot.lastTransaction = participant.lastTransaction;
        for (const auto& [path, file] : session.files)
            snapshot.files.push_back(file);
        send(participant, snapshot);
    }

    void broadcastParticipants(Session& session)
    {
        coop::ParticipantList list;
        for (const auto& [number, participant] : session.participants)
            if (!participant.gone)
                list.participants.push_back({participant.number, participant.name,
                                             participant.colour, participant.connection != 0,
                                             participant.host, participant.recording});
        broadcast(session, list);
    }

    coop::LockState lockState(const Session& session) const
    {
        coop::LockState state;
        state.complete = true;
        for (const auto& [scope, lock] : session.locks)
            state.locks.emplace_back(scope, lock.holder);
        return state;
    }

    void lockChanged(Session& session, const model::EditScope& scope, coop::ParticipantNumber holder)
    {
        coop::LockState change;
        change.locks.emplace_back(scope, holder);
        broadcast(session, change);
    }

    int countPresent(const Session& session) const
    {
        return static_cast<int>(std::count_if(session.participants.begin(), session.participants.end(),
                                              [](const auto& entry) { return !entry.second.gone; }));
    }

    int freeColour(const Session& session) const
    {
        for (int colour = 0; colour < 8; ++colour)
        {
            const bool used = std::any_of(session.participants.begin(), session.participants.end(),
                                          [colour](const auto& entry)
                                          { return !entry.second.gone && entry.second.colour == colour; });
            if (!used)
                return colour;
        }
        return static_cast<int>(session.participants.size() % 8);
    }

    void handleMessage(Session& session, Participant& participant, const coop::Message& message)
    {
        if (const auto* upload = std::get_if<coop::ProjectUpload>(&message))
            handleUpload(session, participant, *upload);
        else if (const auto* transaction = std::get_if<coop::Transaction>(&message))
            handleTransaction(session, participant, *transaction);
        else if (const auto* lock = std::get_if<coop::LockRequest>(&message))
            handleLock(session, participant, *lock);
        else if (const auto* recording = std::get_if<coop::RecordingState>(&message))
        {
            participant.recording = recording->recording;
            broadcastParticipants(session);
        }
        else if (const auto* transport = std::get_if<coop::TransportRequest>(&message))
            broadcast(session, coop::TransportStart{participant.number, transport->play,
                                                    transport->position,
                                                    now + options.transportLead});
        else if (const auto* command = std::get_if<coop::HostCommand>(&message))
            handleHostCommand(session, participant, *command);
        else if (std::holds_alternative<coop::Leave>(message))
            handleLeave(session, participant);
        else if (const auto* presence = std::get_if<coop::PresenceUpdate>(&message))
        {
            participant.presence = presence->presence;
            participant.presencePending = true;
        }
        else if (const auto* ping = std::get_if<coop::Ping>(&message))
            send(participant, coop::Pong{ping->clientTime, now});
        else if (std::holds_alternative<coop::SnapshotRequest>(message) && session.ready)
        {
            write("session " + shortId(session.id) + ": participant "
                  + juce::String(participant.number) + " asked for the project again");
            sendSnapshot(session, participant);
        }
    }

    void handleUpload(Session& session, Participant& participant, const coop::ProjectUpload& upload)
    {
        if (!participant.host || session.ready)
            return;

        auto project = upload.project.createCopy();
        model::removePersonalSettings(project);
        const auto check = model::checkProject(project);
        if (check.failed() || !pathsAreSafe(project))
        {
            write("session " + shortId(session.id) + ": a malformed project was refused");
            send(participant, coop::Rejection{0, coop::RejectionReason::malformed, 0,
                                              check.failed() ? check.getErrorMessage()
                                                             : juce::String("unsafe file paths")});
            return;
        }

        session.project = project;
        for (const auto& file : upload.files)
            session.files[file.path] = file;
        session.ready = true;
        session.sequence = 0;
        session.dirty = true;
        session.journal.clear();
        saveNow(session);
        write("session " + shortId(session.id) + " is ready ("
              + juce::String(model::idsIn(project).size()) + " objects, "
              + juce::String(static_cast<int>(session.files.size())) + " files)");
    }

    void handleTransaction(Session& session, Participant& participant,
                           const coop::Transaction& transaction)
    {
        if (transaction.clientSequence <= participant.lastTransaction)
            return; // dealt with already
        participant.lastTransaction = transaction.clientSequence;
        session.dirty = true;

        const auto reject = [&](coop::RejectionReason reason, coop::ParticipantNumber holder,
                                const juce::String& detail)
        {
            send(participant, coop::Rejection{transaction.clientSequence, reason, holder, detail});
        };

        if (!session.ready)
            return reject(coop::RejectionReason::readOnly, 0, "not ready");

        const auto scopes = model::scopesOf(transaction.changes);
        for (const auto& scope : scopes)
            if (const auto lock = session.locks.find(scope);
                lock != session.locks.end() && lock->second.holder != participant.number)
                return reject(coop::RejectionReason::locked, lock->second.holder, {});

        for (const auto& [number, other] : session.participants)
            if (other.recording && !other.gone && changesWhatRecordingNeeds(transaction.changes))
                return reject(coop::RejectionReason::recording, number, {});

        std::size_t applied = 0;
        const auto rollBack = [&]
        {
            const model::ChangeSet done(transaction.changes.begin(),
                                        transaction.changes.begin() + static_cast<std::ptrdiff_t>(applied));
            [[maybe_unused]] const auto undone = model::applyChanges(session.project, model::inverted(done));
            jassert(undone.wasOk());
        };

        for (const auto& change : transaction.changes)
        {
            if (const auto check = model::checkChange(session.project, change);
                check.failed() || !pathsAreSafe(change))
            {
                rollBack();
                return reject(coop::RejectionReason::malformed, 0,
                              check.failed() ? check.getErrorMessage() : juce::String("unsafe path"));
            }
            if (const auto result = model::applyChanges(session.project, {change}); result.failed())
            {
                rollBack();
                return reject(coop::RejectionReason::conflict, 0, result.getErrorMessage());
            }
            ++applied;
        }

        for (const auto& file : transaction.files)
            if (const auto existing = session.files.find(file.path);
                existing != session.files.end() && !(existing->second == file))
            {
                rollBack();
                return reject(coop::RejectionReason::conflict, 0, "a file of that name exists");
            }
        for (const auto& file : transaction.files)
            session.files[file.path] = file;

        ++session.sequence;
        for (const auto& scope : scopes)
        {
            if (!scopeExists(session.project, scope))
            {
                if (session.locks.erase(scope) > 0)
                    lockChanged(session, scope, 0);
                continue;
            }
            auto& lock = session.locks[scope];
            const bool taken = lock.holder != participant.number;
            lock.holder = participant.number;
            lock.lastUsed = now;
            if (taken)
                lockChanged(session, scope, participant.number);
        }

        coop::AcceptedChange accepted;
        accepted.sequence = session.sequence;
        accepted.author = participant.number;
        accepted.clientSequence = transaction.clientSequence;
        accepted.name = transaction.name;
        accepted.undoStep = transaction.undoStep;
        accepted.changes = transaction.changes;
        accepted.files = transaction.files;
        broadcast(session, accepted);

        if (store != nullptr)
            store->append(session, accepted);
    }

    void handleLock(Session& session, Participant& participant, const coop::LockRequest& request)
    {
        if (!session.ready || !scopeExists(session.project, request.scope))
            return;

        const auto found = session.locks.find(request.scope);
        if (request.hold)
        {
            if (found == session.locks.end())
            {
                session.locks[request.scope] = {participant.number, now};
                lockChanged(session, request.scope, participant.number);
            }
            else if (found->second.holder == participant.number)
                found->second.lastUsed = now;
            else
            {
                coop::LockState state;
                state.locks.emplace_back(request.scope, found->second.holder);
                send(participant, state);
            }
        }
        else if (found != session.locks.end() && found->second.holder == participant.number)
        {
            session.locks.erase(found);
            lockChanged(session, request.scope, 0);
        }
    }

    void releaseLocks(Session& session, coop::ParticipantNumber holder)
    {
        for (auto lock = session.locks.begin(); lock != session.locks.end();)
        {
            if (lock->second.holder == holder)
            {
                const auto scope = lock->first;
                lock = session.locks.erase(lock);
                lockChanged(session, scope, 0);
            }
            else
                ++lock;
        }
    }

    void removeParticipant(Session& session, Participant& participant, bool removedByHost)
    {
        participant.gone = true;
        participant.removed = removedByHost;
        participant.recording = false;
        session.dirty = true;
        releaseLocks(session, participant.number);
        if (removedByHost)
            send(participant, coop::SessionEnded{coop::SessionEnded::Reason::removed});
        if (participant.connection != 0)
            closeConnection(participant.connection, true);
        participant.channelValid = false;
        saveNow(session);
        broadcastParticipants(session);
    }

    void handOver(Session& session, Participant& from, Participant& to)
    {
        from.host = false;
        to.host = true;
        saveNow(session);
        write("session " + shortId(session.id) + ": participant " + juce::String(to.number)
              + " is the host");
        send(to, coop::Invite{session.inviteCode, session.inviteExpiry});
        broadcastParticipants(session);
    }

    void handleHostCommand(Session& session, Participant& participant,
                           const coop::HostCommand& command)
    {
        if (!participant.host)
            return;

        const auto target = session.participants.find(command.participant);
        const bool validTarget = target != session.participants.end() && !target->second.gone
                                 && target->second.number != participant.number;

        switch (command.kind)
        {
        case coop::HostCommand::Kind::newInvite:
            session.inviteCode = newInviteCode();
            session.inviteExpiry = now + options.inviteLifetime;
            saveNow(session);
            send(participant, coop::Invite{session.inviteCode, session.inviteExpiry});
            break;
        case coop::HostCommand::Kind::revokeInvite:
            session.inviteCode = {};
            session.inviteExpiry = 0.0;
            saveNow(session);
            send(participant, coop::Invite{});
            break;
        case coop::HostCommand::Kind::remove:
            if (validTarget)
            {
                write("session " + shortId(session.id) + ": participant "
                      + juce::String(command.participant) + " removed by the host");
                removeParticipant(session, target->second, true);
            }
            break;
        case coop::HostCommand::Kind::handOver:
            if (validTarget)
                handOver(session, participant, target->second);
            break;
        case coop::HostCommand::Kind::close:
            endSession(session);
            break;
        }
    }

    void handleLeave(Session& session, Participant& participant)
    {
        if (participant.host)
        {
            Participant* next = nullptr;
            for (auto& [number, other] : session.participants)
                if (!other.gone && number != participant.number
                    && (next == nullptr || (other.connection != 0 && next->connection == 0)))
                    next = &other;
            if (next == nullptr)
                return endSession(session);
            handOver(session, participant, *next);
        }
        write("session " + shortId(session.id) + ": participant "
              + juce::String(participant.number) + " left");
        removeParticipant(session, participant, false);
    }

    void endSession(Session& session)
    {
        if (session.ended)
            return;
        session.ended = true;
        for (auto& [number, participant] : session.participants)
            if (!participant.gone)
                send(participant, coop::SessionEnded{coop::SessionEnded::Reason::closed});
        session.endAt = now + closingDelay;
        write("session " + shortId(session.id) + " closed by the host");
    }

    // The loop ---------------------------------------------------------------------------

    void update(double time)
    {
        now = time;

        std::vector<std::uint64_t> ids;
        for (const auto& [id, connection] : connections)
            ids.push_back(id);

        for (const auto id : ids)
        {
            auto found = connections.find(id);
            if (found == connections.end())
                continue;
            for (const auto& frame : found->second.link->receive())
            {
                if (connections.count(id) == 0 || connections.at(id).closing)
                    break;
                handleFrame(id, frame);
            }
            found = connections.find(id);
            if (found != connections.end() && !found->second.closing
                && found->second.link->getState() == net::Link::State::closed)
                closeConnection(id, false);
        }

        expire();
        relayPresence();
        flush();
        removeClosed();
        endSessions();
        save(false);
    }

    void expire()
    {
        for (auto& [id, connection] : connections)
            if (!connection.closing && connection.session.isEmpty()
                && now - connection.openedAt > options.helloTimeout)
            {
                connection.closing = true;
                connection.closeAt = now;
            }

        std::vector<juce::String> abandoned;
        for (auto& [sessionId, session] : sessions)
        {
            bool anyoneHere = false;
            for (auto& [number, participant] : session.participants)
            {
                if (participant.connection != 0)
                    anyoneHere = true;
                else if (participant.recording && now - participant.disconnectedAt > options.disconnectGrace)
                {
                    participant.recording = false;
                    broadcastParticipants(session);
                }
            }
            if (anyoneHere)
                session.lastActive = now;

            for (auto lock = session.locks.begin(); lock != session.locks.end();)
            {
                const auto holder = session.participants.find(lock->second.holder);
                const bool keep =
                    holder != session.participants.end() && !holder->second.gone
                    && (holder->second.recording
                        || (now - lock->second.lastUsed <= options.lockIdleTimeout
                            && (holder->second.connection != 0
                                || now - holder->second.disconnectedAt <= options.disconnectGrace)));
                if (keep)
                {
                    ++lock;
                    continue;
                }
                const auto scope = lock->first;
                lock = session.locks.erase(lock);
                lockChanged(session, scope, 0);
            }

            if (!anyoneHere && now - session.lastActive > options.abandonedLifetime)
                abandoned.push_back(sessionId);
        }

        for (const auto& sessionId : abandoned)
        {
            write("session " + shortId(sessionId) + " deleted: nobody came for too long");
            deleteSession(sessionId);
        }

        for (auto failure = failures.begin(); failure != failures.end();)
        {
            auto& times = failure->second;
            times.erase(std::remove_if(times.begin(), times.end(),
                                       [this](double when) { return now - when > options.failedAttemptsWindow; }),
                        times.end());
            failure = times.empty() ? failures.erase(failure) : std::next(failure);
        }
        for (auto blocked = blockedUntil.begin(); blocked != blockedUntil.end();)
            blocked = blocked->second <= now ? blockedUntil.erase(blocked) : std::next(blocked);
    }

    void relayPresence()
    {
        for (auto& [sessionId, session] : sessions)
            for (auto& [number, participant] : session.participants)
            {
                if (!participant.presencePending
                    || now - participant.presenceSentAt < options.presenceInterval)
                    continue;
                participant.presencePending = false;
                participant.presenceSentAt = now;
                for (auto& [otherNumber, other] : session.participants)
                    if (otherNumber != number)
                        send(other, coop::PresenceUpdate{number, participant.presence});
            }
    }

    void flush()
    {
        for (auto& [id, connection] : connections)
        {
            if (connection.session.isEmpty())
                continue;
            const auto session = sessions.find(connection.session);
            if (session == sessions.end())
                continue;
            auto& participant = session->second.participants.at(connection.participant);
            for (auto& frame : participant.channel.takeFrames(now))
                connection.link->send(coop::channelFrame(frame));
        }
    }

    void removeClosed()
    {
        for (auto connection = connections.begin(); connection != connections.end();)
        {
            if (connection->second.closing && now >= connection->second.closeAt)
            {
                connection->second.link->close();
                connection = connections.erase(connection);
            }
            else
                ++connection;
        }
    }

    void endSessions()
    {
        std::vector<juce::String> ended;
        for (const auto& [sessionId, session] : sessions)
            if (session.ended && now >= session.endAt)
                ended.push_back(sessionId);
        for (const auto& sessionId : ended)
            deleteSession(sessionId);
    }

    void deleteSession(const juce::String& sessionId)
    {
        for (auto& [id, connection] : connections)
            if (connection.session == sessionId)
            {
                connection.session = {};
                connection.closing = true;
                connection.closeAt = now;
            }
        sessions.erase(sessionId);
        closedSessions.insert(sessionId);
        if (store != nullptr)
        {
            store->remove(sessionId);
            store->saveClosedSessions(closedSessions);
        }
    }

    // Who may come back and with which code matters more than a few seconds of waiting.
    void saveNow(Session& session)
    {
        session.dirty = true;
        if (store == nullptr)
            return;
        if (!store->save(session))
            write("session " + shortId(session.id) + " could not be saved");
        session.dirty = false;
        session.savedAt = now;
    }

    void save(bool everything)
    {
        if (store == nullptr)
            return;
        for (auto& [sessionId, session] : sessions)
            if (session.dirty && (everything || now - session.savedAt >= options.saveInterval))
            {
                if (!store->save(session))
                    write("session " + shortId(sessionId) + " could not be saved");
                session.dirty = false;
                session.savedAt = now;
            }
    }

    ServerOptions options;
    std::unique_ptr<SessionStore> store;
    std::map<juce::String, Session> sessions;
    std::map<std::uint64_t, Connection> connections;
    std::uint64_t nextConnection = 1;
    std::map<juce::String, std::vector<double>> failures;
    std::map<juce::String, double> blockedUntil;
    std::set<juce::String> closedSessions;
    double now = 0.0;
};

// Server --------------------------------------------------------------------------------

Server::Server(ServerOptions options)
    : impl(std::make_unique<Impl>(std::move(options)))
{
    net::initialiseTls();
}

Server::~Server()
{
    saveAll();
}

void Server::addConnection(std::unique_ptr<net::Link> link, const juce::String& address, double now)
{
    impl->addConnection(std::move(link), address, now);
}

void Server::update(double now)
{
    impl->update(now);
}

void Server::saveAll()
{
    impl->save(true);
}

int Server::getNumSessions() const
{
    return static_cast<int>(impl->sessions.size());
}

int Server::getNumConnections() const
{
    return static_cast<int>(impl->connections.size());
}

juce::ValueTree Server::getProject(const juce::String& sessionId) const
{
    const auto found = impl->sessions.find(sessionId);
    return found != impl->sessions.end() ? found->second.project : juce::ValueTree();
}

std::uint64_t Server::getSequence(const juce::String& sessionId) const
{
    const auto found = impl->sessions.find(sessionId);
    return found != impl->sessions.end() ? found->second.sequence : 0;
}

std::vector<std::pair<model::EditScope, coop::ParticipantNumber>>
Server::getLocks(const juce::String& sessionId) const
{
    std::vector<std::pair<model::EditScope, coop::ParticipantNumber>> locks;
    if (const auto found = impl->sessions.find(sessionId); found != impl->sessions.end())
        for (const auto& [scope, lock] : found->second.locks)
            locks.emplace_back(scope, lock.holder);
    return locks;
}

juce::StringArray Server::getSessionIds() const
{
    juce::StringArray ids;
    for (const auto& [sessionId, session] : impl->sessions)
        ids.add(sessionId);
    return ids;
}

} // namespace daw::server
