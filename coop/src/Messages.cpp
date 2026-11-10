#include "daw/coop/Messages.h"

#include "daw/coop/Wire.h"

#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"

#include <type_traits>

namespace daw::coop
{

namespace
{

// Project trees in uploads and snapshots are compressed: plugin states in Base64 and
// repeated property names shrink a lot.
constexpr std::size_t maximumTreeSize = 256u * 1024u * 1024u;

void writeCompressedTree(Writer& writer, const juce::ValueTree& tree)
{
    Writer plain;
    plain.writeTree(tree);
    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream zip(compressed, 6);
        zip.write(plain.getData().getData(), plain.getData().getSize());
    }
    writer.writeVarUInt(plain.getData().getSize());
    writer.writeBlock(compressed.getMemoryBlock());
}

juce::ValueTree readCompressedTree(Reader& reader)
{
    const auto plainSize = reader.readVarUInt();
    const auto compressed = reader.readBlock();
    if (!reader.isOk() || plainSize > maximumTreeSize)
    {
        reader.fail();
        return {};
    }

    // Read in pieces up to one byte more than announced, so that what the data claims
    // never decides how much memory is taken.
    juce::MemoryInputStream source(compressed, false);
    juce::GZIPDecompressorInputStream unzip(source);
    juce::MemoryOutputStream plainStream;
    constexpr int pieceSize = 1 << 20;
    juce::HeapBlock<char> piece(pieceSize);
    while (plainStream.getDataSize() <= plainSize)
    {
        const auto read = unzip.read(piece.get(), pieceSize);
        if (read <= 0)
            break;
        plainStream.write(piece.get(), static_cast<std::size_t>(read));
    }
    if (plainStream.getDataSize() != plainSize)
    {
        reader.fail();
        return {};
    }
    const auto plain = plainStream.getMemoryBlock();

    Reader treeReader(plain);
    auto tree = treeReader.readTree();
    if (!treeReader.isOk() || !treeReader.isAtEnd())
    {
        reader.fail();
        return {};
    }
    return tree;
}

void writeFiles(Writer& writer, const std::vector<FileEntry>& files)
{
    writer.writeVarUInt(files.size());
    for (const auto& file : files)
    {
        writer.writeString(file.path);
        writer.writeBlock(file.hash);
        writer.writeVarUInt(file.size);
    }
}

std::vector<FileEntry> readFiles(Reader& reader)
{
    std::vector<FileEntry> files;
    const auto count = reader.readCount(3);
    for (std::size_t index = 0; index < count && reader.isOk(); ++index)
    {
        FileEntry file;
        file.path = reader.readString();
        file.hash = reader.readBlock();
        file.size = reader.readVarUInt();
        if (file.hash.getSize() != 32)
            reader.fail();
        files.push_back(std::move(file));
    }
    return files;
}

void writeScope(Writer& writer, const model::EditScope& scope)
{
    writer.writeByte(static_cast<std::uint8_t>(scope.kind));
    writer.writeVarInt(scope.id);
}

model::EditScope readScope(Reader& reader)
{
    model::EditScope scope;
    const auto kind = reader.readByte();
    if (kind > static_cast<std::uint8_t>(model::EditScope::Kind::bus))
        reader.fail();
    scope.kind = static_cast<model::EditScope::Kind>(kind);
    scope.id = reader.readVarInt();
    return scope;
}

void writeIds(Writer& writer, const std::vector<model::ObjectId>& ids)
{
    writer.writeVarUInt(ids.size());
    for (const auto id : ids)
        writer.writeVarInt(id);
}

std::vector<model::ObjectId> readIds(Reader& reader)
{
    std::vector<model::ObjectId> ids;
    const auto count = reader.readCount();
    for (std::size_t index = 0; index < count && reader.isOk(); ++index)
        ids.push_back(reader.readVarInt());
    return ids;
}

template <typename Enum>
Enum readEnum(Reader& reader, Enum last)
{
    const auto value = reader.readByte();
    if (value > static_cast<std::uint8_t>(last))
        reader.fail();
    return static_cast<Enum>(value);
}

std::uint32_t readParticipant(Reader& reader)
{
    const auto value = reader.readVarUInt();
    if (value > std::numeric_limits<std::uint32_t>::max())
        reader.fail();
    return static_cast<std::uint32_t>(value);
}

void writePresence(Writer& writer, const Presence& presence)
{
    writer.writeVarInt(presence.track);
    writer.writeBool(presence.hasCursor);
    writer.writeVarInt(presence.cursorTrack);
    writer.writeVarInt(presence.cursorPosition);
    writer.writeVarInt(presence.clip);
    writer.writeVarInt(presence.cursorKey);
    writer.writeVarInt(presence.clipCursorPosition);
    writeIds(writer, presence.selectedClips);
    writer.writeVarUInt(presence.selectedNotes.size());
    for (const auto note : presence.selectedNotes)
        writer.writeVarInt(note);
    writer.writeBool(presence.playing);
    writer.writeVarInt(presence.playhead);
}

Presence readPresence(Reader& reader)
{
    Presence presence;
    presence.track = reader.readVarInt();
    presence.hasCursor = reader.readBool();
    presence.cursorTrack = reader.readVarInt();
    presence.cursorPosition = reader.readVarInt();
    presence.clip = reader.readVarInt();
    presence.cursorKey = static_cast<int>(std::clamp<std::int64_t>(reader.readVarInt(), -1, 127));
    presence.clipCursorPosition = reader.readVarInt();
    presence.selectedClips = readIds(reader);
    const auto notes = reader.readCount();
    for (std::size_t index = 0; index < notes && reader.isOk(); ++index)
        presence.selectedNotes.push_back(static_cast<std::int32_t>(
            std::clamp<std::int64_t>(reader.readVarInt(), std::numeric_limits<std::int32_t>::min(),
                                     std::numeric_limits<std::int32_t>::max())));
    presence.playing = reader.readBool();
    presence.playhead = reader.readVarInt();
    return presence;
}

// Writing each message ------------------------------------------------------------------

void write(Writer& writer, const ProjectUpload& message)
{
    writeCompressedTree(writer, message.project);
    writeFiles(writer, message.files);
}

void write(Writer& writer, const Transaction& message)
{
    writer.writeVarUInt(message.clientSequence);
    writer.writeString(message.name);
    writer.writeVarUInt(message.undoStep);
    writer.writeChanges(message.changes);
    writeFiles(writer, message.files);
}

void write(Writer& writer, const LockRequest& message)
{
    writeScope(writer, message.scope);
    writer.writeBool(message.hold);
}

void write(Writer& writer, const RecordingState& message)
{
    writer.writeBool(message.recording);
}

void write(Writer& writer, const TransportRequest& message)
{
    writer.writeBool(message.play);
    writer.writeVarInt(message.position);
}

void write(Writer& writer, const HostCommand& message)
{
    writer.writeByte(static_cast<std::uint8_t>(message.kind));
    writer.writeVarUInt(message.participant);
}

void write(Writer&, const Leave&) {}

void write(Writer& writer, const ProjectSnapshot& message)
{
    writer.writeVarUInt(message.sequence);
    writeCompressedTree(writer, message.project);
    writeFiles(writer, message.files);
}

void write(Writer& writer, const ParticipantList& message)
{
    writer.writeVarUInt(message.participants.size());
    for (const auto& participant : message.participants)
    {
        writer.writeVarUInt(participant.number);
        writer.writeString(participant.name);
        writer.writeVarUInt(static_cast<std::uint64_t>(participant.colour));
        writer.writeBool(participant.connected);
        writer.writeBool(participant.host);
        writer.writeBool(participant.recording);
    }
}

void write(Writer& writer, const LockState& message)
{
    writer.writeBool(message.complete);
    writer.writeVarUInt(message.locks.size());
    for (const auto& [scope, holder] : message.locks)
    {
        writeScope(writer, scope);
        writer.writeVarUInt(holder);
    }
}

void write(Writer& writer, const AcceptedChange& message)
{
    writer.writeVarUInt(message.sequence);
    writer.writeVarUInt(message.author);
    writer.writeVarUInt(message.clientSequence);
    writer.writeString(message.name);
    writer.writeVarUInt(message.undoStep);
    writer.writeChanges(message.changes);
    writeFiles(writer, message.files);
}

void write(Writer& writer, const Rejection& message)
{
    writer.writeVarUInt(message.clientSequence);
    writer.writeByte(static_cast<std::uint8_t>(message.reason));
    writer.writeVarUInt(message.holder);
    writer.writeString(message.detail);
}

void write(Writer& writer, const TransportStart& message)
{
    writer.writeVarUInt(message.by);
    writer.writeBool(message.play);
    writer.writeVarInt(message.position);
    writer.writeDouble(message.startTime);
}

void write(Writer& writer, const Invite& message)
{
    writer.writeString(message.code);
    writer.writeDouble(message.expiryTime);
}

void write(Writer& writer, const FileAvailable& message)
{
    writer.writeBlock(message.hash);
}

void write(Writer& writer, const SessionEnded& message)
{
    writer.writeByte(static_cast<std::uint8_t>(message.reason));
}

void write(Writer& writer, const PresenceUpdate& message)
{
    writer.writeVarUInt(message.participant);
    writePresence(writer, message.presence);
}

void write(Writer& writer, const Ping& message)
{
    writer.writeDouble(message.clientTime);
}

void write(Writer& writer, const Pong& message)
{
    writer.writeDouble(message.clientTime);
    writer.writeDouble(message.serverTime);
}

// Reading each message ------------------------------------------------------------------

template <typename Type>
Type read(Reader& reader);

template <>
ProjectUpload read(Reader& reader)
{
    ProjectUpload message;
    message.project = readCompressedTree(reader);
    message.files = readFiles(reader);
    return message;
}

template <>
Transaction read(Reader& reader)
{
    Transaction message;
    message.clientSequence = reader.readVarUInt();
    message.name = reader.readString();
    message.undoStep = reader.readVarUInt();
    message.changes = reader.readChanges();
    message.files = readFiles(reader);
    return message;
}

template <>
LockRequest read(Reader& reader)
{
    LockRequest message;
    message.scope = readScope(reader);
    message.hold = reader.readBool();
    return message;
}

template <>
RecordingState read(Reader& reader)
{
    return {reader.readBool()};
}

template <>
TransportRequest read(Reader& reader)
{
    TransportRequest message;
    message.play = reader.readBool();
    message.position = reader.readVarInt();
    return message;
}

template <>
HostCommand read(Reader& reader)
{
    HostCommand message;
    message.kind = readEnum(reader, HostCommand::Kind::close);
    message.participant = readParticipant(reader);
    return message;
}

template <>
Leave read(Reader&)
{
    return {};
}

template <>
ProjectSnapshot read(Reader& reader)
{
    ProjectSnapshot message;
    message.sequence = reader.readVarUInt();
    message.project = readCompressedTree(reader);
    message.files = readFiles(reader);
    return message;
}

template <>
ParticipantList read(Reader& reader)
{
    ParticipantList message;
    const auto count = reader.readCount(6);
    for (std::size_t index = 0; index < count && reader.isOk(); ++index)
    {
        ParticipantInfo participant;
        participant.number = readParticipant(reader);
        participant.name = reader.readString();
        participant.colour = static_cast<int>(std::min<std::uint64_t>(reader.readVarUInt(), 7));
        participant.connected = reader.readBool();
        participant.host = reader.readBool();
        participant.recording = reader.readBool();
        message.participants.push_back(std::move(participant));
    }
    return message;
}

template <>
LockState read(Reader& reader)
{
    LockState message;
    message.complete = reader.readBool();
    const auto count = reader.readCount(3);
    for (std::size_t index = 0; index < count && reader.isOk(); ++index)
    {
        const auto scope = readScope(reader);
        const auto holder = readParticipant(reader);
        message.locks.emplace_back(scope, holder);
    }
    return message;
}

template <>
AcceptedChange read(Reader& reader)
{
    AcceptedChange message;
    message.sequence = reader.readVarUInt();
    message.author = readParticipant(reader);
    message.clientSequence = reader.readVarUInt();
    message.name = reader.readString();
    message.undoStep = reader.readVarUInt();
    message.changes = reader.readChanges();
    message.files = readFiles(reader);
    return message;
}

template <>
Rejection read(Reader& reader)
{
    Rejection message;
    message.clientSequence = reader.readVarUInt();
    message.reason = readEnum(reader, RejectionReason::readOnly);
    message.holder = readParticipant(reader);
    message.detail = reader.readString();
    return message;
}

template <>
TransportStart read(Reader& reader)
{
    TransportStart message;
    message.by = readParticipant(reader);
    message.play = reader.readBool();
    message.position = reader.readVarInt();
    message.startTime = reader.readDouble();
    return message;
}

template <>
Invite read(Reader& reader)
{
    Invite message;
    message.code = reader.readString();
    message.expiryTime = reader.readDouble();
    return message;
}

template <>
FileAvailable read(Reader& reader)
{
    return {reader.readBlock()};
}

template <>
SessionEnded read(Reader& reader)
{
    return {readEnum(reader, SessionEnded::Reason::closed)};
}

template <>
PresenceUpdate read(Reader& reader)
{
    PresenceUpdate message;
    message.participant = readParticipant(reader);
    message.presence = readPresence(reader);
    return message;
}

template <>
Ping read(Reader& reader)
{
    return {reader.readDouble()};
}

template <>
Pong read(Reader& reader)
{
    Pong message;
    message.clientTime = reader.readDouble();
    message.serverTime = reader.readDouble();
    return message;
}

template <std::size_t index = 0>
std::optional<Message> readAlternative(Reader& reader, std::size_t tag)
{
    if constexpr (index < std::variant_size_v<Message>)
    {
        if (tag == index)
            return Message{std::in_place_index<index>,
                           read<std::variant_alternative_t<index, Message>>(reader)};
        return readAlternative<index + 1>(reader, tag);
    }
    else
    {
        juce::ignoreUnused(reader, tag);
        return std::nullopt;
    }
}

void writeHelloRequest(Writer& writer, const Hello& hello)
{
    writer.writeByte(static_cast<std::uint8_t>(hello.request.index()));
    if (const auto* create = std::get_if<CreateSession>(&hello.request))
    {
        writer.writeString(create->hostKey);
        writer.writeString(create->name);
    }
    else if (const auto* join = std::get_if<JoinSession>(&hello.request))
    {
        writer.writeString(join->inviteCode);
        writer.writeString(join->name);
    }
    else if (const auto* rejoin = std::get_if<RejoinSession>(&hello.request))
    {
        writer.writeString(rejoin->sessionId);
        writer.writeVarUInt(rejoin->participant);
        writer.writeString(rejoin->token);
    }
}

} // namespace

bool isUnreliable(const Message& message) noexcept
{
    return std::holds_alternative<PresenceUpdate>(message) || std::holds_alternative<Ping>(message)
           || std::holds_alternative<Pong>(message);
}

juce::MemoryBlock encode(const Message& message)
{
    Writer writer;
    writer.writeByte(static_cast<std::uint8_t>(message.index()));
    std::visit([&writer](const auto& alternative) { write(writer, alternative); }, message);
    return writer.takeData();
}

std::optional<Message> decodeMessage(const juce::MemoryBlock& data)
{
    Reader reader(data);
    const auto tag = reader.readByte();
    if (!reader.isOk())
        return std::nullopt;

    auto message = readAlternative(reader, tag);
    if (!message || !reader.isOk() || !reader.isAtEnd())
        return std::nullopt;
    return message;
}

juce::MemoryBlock encodeFrame(const Hello& hello)
{
    Writer writer;
    writer.writeByte(static_cast<std::uint8_t>(FrameKind::hello));
    writer.writeUInt32(helloMagic);
    writer.writeUInt32(hello.protocolVersion);
    writer.writeUInt32(hello.formatVersion);
    writer.writeString(hello.appVersion);
    writeHelloRequest(writer, hello);
    writer.writeVarUInt(hello.delivered);
    return writer.takeData();
}

juce::MemoryBlock encodeFrame(const Welcome& welcome)
{
    Writer writer;
    writer.writeByte(static_cast<std::uint8_t>(FrameKind::welcome));
    writer.writeString(welcome.sessionId);
    writer.writeVarUInt(welcome.participant);
    writer.writeString(welcome.token);
    writer.writeBool(welcome.resumed);
    writer.writeVarUInt(welcome.delivered);
    writer.writeDouble(welcome.serverTime);
    return writer.takeData();
}

juce::MemoryBlock encodeFrame(const Refused& refused)
{
    Writer writer;
    writer.writeByte(static_cast<std::uint8_t>(FrameKind::refused));
    writer.writeByte(static_cast<std::uint8_t>(refused.reason));
    writer.writeUInt32(refused.serverProtocol);
    writer.writeUInt32(refused.serverFormat);
    writer.writeString(refused.detail);
    return writer.takeData();
}

std::optional<FrameKind> frameKindOf(const juce::MemoryBlock& frame)
{
    if (frame.getSize() == 0)
        return std::nullopt;
    const auto kind = static_cast<std::uint8_t>(frame[0]);
    if (kind < static_cast<std::uint8_t>(FrameKind::hello)
        || kind > static_cast<std::uint8_t>(FrameKind::channel))
        return std::nullopt;
    return static_cast<FrameKind>(kind);
}

std::optional<HelloVersions> readHelloVersions(const juce::MemoryBlock& frame)
{
    Reader reader(frame);
    if (reader.readByte() != static_cast<std::uint8_t>(FrameKind::hello)
        || reader.readUInt32() != helloMagic)
        return std::nullopt;

    HelloVersions versions;
    versions.protocolVersion = reader.readUInt32();
    versions.formatVersion = reader.readUInt32();
    if (!reader.isOk())
        return std::nullopt;
    return versions;
}

std::optional<Hello> decodeHello(const juce::MemoryBlock& frame)
{
    const auto versions = readHelloVersions(frame);
    if (!versions)
        return std::nullopt;

    // The kind, the magic and the two versions, read above.
    Reader reader(static_cast<const char*>(frame.getData()) + 13, frame.getSize() - 13);

    Hello hello;
    hello.protocolVersion = versions->protocolVersion;
    hello.formatVersion = versions->formatVersion;
    hello.appVersion = reader.readString();
    switch (reader.readByte())
    {
    case 0:
    {
        CreateSession create;
        create.hostKey = reader.readString();
        create.name = reader.readString();
        hello.request = create;
        break;
    }
    case 1:
    {
        JoinSession join;
        join.inviteCode = reader.readString();
        join.name = reader.readString();
        hello.request = join;
        break;
    }
    case 2:
    {
        RejoinSession rejoin;
        rejoin.sessionId = reader.readString();
        rejoin.participant = readParticipant(reader);
        rejoin.token = reader.readString();
        hello.request = rejoin;
        break;
    }
    default:
        return std::nullopt;
    }
    hello.delivered = reader.readVarUInt();

    if (!reader.isOk() || !reader.isAtEnd())
        return std::nullopt;
    return hello;
}

std::optional<Welcome> decodeWelcome(const juce::MemoryBlock& frame)
{
    Reader reader(frame);
    if (reader.readByte() != static_cast<std::uint8_t>(FrameKind::welcome))
        return std::nullopt;

    Welcome welcome;
    welcome.sessionId = reader.readString();
    welcome.participant = readParticipant(reader);
    welcome.token = reader.readString();
    welcome.resumed = reader.readBool();
    welcome.delivered = reader.readVarUInt();
    welcome.serverTime = reader.readDouble();
    if (!reader.isOk() || !reader.isAtEnd())
        return std::nullopt;
    return welcome;
}

std::optional<Refused> decodeRefused(const juce::MemoryBlock& frame)
{
    Reader reader(frame);
    if (reader.readByte() != static_cast<std::uint8_t>(FrameKind::refused))
        return std::nullopt;

    Refused refused;
    const auto reason = reader.readByte();
    refused.reason = reason > static_cast<std::uint8_t>(RefusalReason::malformed)
                         ? RefusalReason::malformed // a reason from a newer version
                         : static_cast<RefusalReason>(reason);
    refused.serverProtocol = reader.readUInt32();
    refused.serverFormat = reader.readUInt32();
    refused.detail = reader.readString();
    if (!reader.isOk())
        return std::nullopt;
    return refused;
}

juce::MemoryBlock channelFrame(const juce::MemoryBlock& payload)
{
    juce::MemoryBlock frame;
    const auto kind = static_cast<std::uint8_t>(FrameKind::channel);
    frame.append(&kind, 1);
    frame.append(payload.getData(), payload.getSize());
    return frame;
}

juce::MemoryBlock channelPayload(const juce::MemoryBlock& frame)
{
    if (frame.getSize() < 1)
        return {};
    return {static_cast<const char*>(frame.getData()) + 1, frame.getSize() - 1};
}

juce::String describe(const Refused& refused)
{
    const auto ourProtocol = static_cast<std::uint32_t>(model::networkProtocolVersion);
    const auto ourFormat = static_cast<std::uint32_t>(model::currentFormatVersion);

    switch (refused.reason)
    {
    case RefusalReason::incompatibleProtocol:
        return refused.serverProtocol > ourProtocol
                   ? "This co-op server needs a newer version of MICINSTATION. Update the app "
                     "(Help > Check for Updates) and try again."
                   : "This co-op server runs an older version than this app. Ask whoever runs it "
                     "to update it.";
    case RefusalReason::incompatibleProjectFormat:
        return refused.serverFormat > ourFormat
                   ? "This session uses projects of a newer version of MICINSTATION. Update the "
                     "app (Help > Check for Updates) and try again."
                   : "This session uses projects of an older version than this app. Ask whoever "
                     "runs the server to update it.";
    case RefusalReason::badHostKey:
        return "This server does not accept the host key you entered.";
    case RefusalReason::badInvite:
        return "This invite code is not valid. Check it, or ask the host for a new one.";
    case RefusalReason::expiredInvite:
        return "This invite has expired. Ask the host for a new one.";
    case RefusalReason::badToken:
        return "This session does not know you any more. Join again with an invite.";
    case RefusalReason::removed:
        return "The host removed you from this session.";
    case RefusalReason::sessionFull:
        return "This session is full.";
    case RefusalReason::sessionClosed:
        return "The host closed this session.";
    case RefusalReason::tooManyAttempts:
        return "Too many wrong codes came from your connection. Try again in 15 minutes.";
    case RefusalReason::serverFull:
        return "The co-op server is full. Try again later.";
    case RefusalReason::notReady:
        return "The session is still being set up. Try again in a moment.";
    case RefusalReason::malformed:
        break;
    }
    return "The co-op server could not understand this app.";
}

} // namespace daw::coop
