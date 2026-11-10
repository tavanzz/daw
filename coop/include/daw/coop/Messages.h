#pragma once

#include "daw/model/ProjectChanges.h"
#include "daw/model/TimelineTime.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace daw::coop
{

/**
    What the copies of the app and the server of a co-op session say to each other
    (network protocol version model::networkProtocolVersion).

    Every frame on a connection starts with a FrameKind. A connection opens with the
    client's Hello, answered by Welcome or Refused; then both sides send channel frames
    (net::ReliableChannel) carrying the Messages below, reliably and in order, or (presence,
    clocks) as they come. The beginning of Hello (its kind, the magic, the protocol and
    project format versions) and the whole of Refused must never change, so that any two
    versions can tell each other why they cannot talk.
*/

enum class FrameKind : std::uint8_t
{
    hello = 1,
    welcome = 2,
    refused = 3,
    channel = 4
};

/** "MICN": the first bytes of a hello after its kind. */
inline constexpr std::uint32_t helloMagic = 0x4E43494Du;

/** Participants are numbered from 1 in the order they first joined; 0 is nobody. */
using ParticipantNumber = std::uint32_t;

/** A file a project refers to: its path relative to the copy, the SHA-256 of its content
    and its size. */
struct FileEntry
{
    juce::String path;
    juce::MemoryBlock hash;
    std::uint64_t size = 0;

    bool operator==(const FileEntry&) const = default;
};

// The handshake ------------------------------------------------------------------------

/** Opens a new session with the host's project (sent next, as ProjectUpload). */
struct CreateSession
{
    juce::String hostKey;
    juce::String name; ///< the participant's
};

struct JoinSession
{
    juce::String inviteCode;
    juce::String name;
};

/** Back after a disconnection, or after the app started again. */
struct RejoinSession
{
    juce::String sessionId;
    ParticipantNumber participant = 0;
    juce::String token;
};

struct Hello
{
    std::uint32_t protocolVersion = 0;
    std::uint32_t formatVersion = 0;
    juce::String appVersion;
    std::variant<CreateSession, JoinSession, RejoinSession> request;
    /** How many reliable messages of this session the client already received. */
    std::uint64_t delivered = 0;
};

struct Welcome
{
    juce::String sessionId;
    ParticipantNumber participant = 0;
    juce::String token; ///< for rejoining
    /** True if the server kept this participant's channel: both sides send again only what
        the other lacks. Otherwise the client gets a ProjectSnapshot first. */
    bool resumed = false;
    std::uint64_t delivered = 0; ///< reliable messages from the client the server has
    double serverTime = 0.0;     ///< seconds on the server's clock
};

enum class RefusalReason : std::uint8_t
{
    incompatibleProtocol = 0,
    incompatibleProjectFormat = 1,
    badHostKey = 2,
    badInvite = 3,
    expiredInvite = 4,
    badToken = 5,
    removed = 6,
    sessionFull = 7,
    sessionClosed = 8,
    tooManyAttempts = 9,
    serverFull = 10,
    notReady = 11,
    malformed = 12
};

struct Refused
{
    RefusalReason reason = RefusalReason::malformed;
    std::uint32_t serverProtocol = 0;
    std::uint32_t serverFormat = 0;
    juce::String detail;
};

// Messages from a participant to the server ------------------------------------------------

/** The host's project in the shared form (model::sharedCopy()), with its files. */
struct ProjectUpload
{
    juce::ValueTree project;
    std::vector<FileEntry> files;
};

/** One transaction of a participant: changes in the shared form, in their order. */
struct Transaction
{
    std::uint64_t clientSequence = 0; ///< 1, 2, ... per participant
    juce::String name;                ///< "Move Clip"
    std::uint64_t undoStep = 0;       ///< the participant's undo step it belongs to (0: none)
    model::ChangeSet changes;
    std::vector<FileEntry> files; ///< files the changes start to refer to
};

/** Takes (hold) or keeps a lock, or lets it go. */
struct LockRequest
{
    model::EditScope scope;
    bool hold = true;
};

struct RecordingState
{
    bool recording = false;
};

/** Play or stop for everyone who follows the shared transport. */
struct TransportRequest
{
    bool play = false;
    model::Ticks position = 0;
};

struct HostCommand
{
    enum class Kind : std::uint8_t
    {
        newInvite,
        revokeInvite,
        remove,
        handOver,
        close
    };

    Kind kind = Kind::newInvite;
    ParticipantNumber participant = 0; ///< for remove and handOver
};

/** The participant leaves for good (their token stops working). */
struct Leave
{
};

// Messages from the server ---------------------------------------------------------------

/** The whole shared project after `sequence` changes, for a participant joining or out of
    step. */
struct ProjectSnapshot
{
    std::uint64_t sequence = 0;
    juce::ValueTree project;
    std::vector<FileEntry> files;
};

struct ParticipantInfo
{
    ParticipantNumber number = 0;
    juce::String name;
    int colour = 0; ///< 0 to 7, into the theme's participant colours
    bool connected = false;
    bool host = false;
    bool recording = false;

    bool operator==(const ParticipantInfo&) const = default;
};

struct ParticipantList
{
    std::vector<ParticipantInfo> participants;
};

/** Who holds which lock: all of them (complete), or the ones that changed (a holder 0
    means free). */
struct LockState
{
    std::vector<std::pair<model::EditScope, ParticipantNumber>> locks;
    bool complete = false;
};

/** A transaction the server accepted, number `sequence` in the session's one order. */
struct AcceptedChange
{
    std::uint64_t sequence = 0;
    ParticipantNumber author = 0;
    std::uint64_t clientSequence = 0;
    juce::String name;
    std::uint64_t undoStep = 0;
    model::ChangeSet changes;
    std::vector<FileEntry> files;
};

enum class RejectionReason : std::uint8_t
{
    locked = 0,    ///< someone else holds a lock it needs (holder)
    conflict = 1,  ///< the project is not what it expects
    malformed = 2, ///< it would make the project malformed
    recording = 3, ///< not while someone records (holder)
    readOnly = 4
};

/** Sent to the author only. */
struct Rejection
{
    std::uint64_t clientSequence = 0;
    RejectionReason reason = RejectionReason::conflict;
    ParticipantNumber holder = 0;
    juce::String detail;
};

/** Everyone who follows starts (or stops) at startTime on the server's clock. */
struct TransportStart
{
    ParticipantNumber by = 0;
    bool play = false;
    model::Ticks position = 0;
    double startTime = 0.0;
};

/** To the host: the session's invite code. Empty after it was revoked. */
struct Invite
{
    juce::String code;
    double expiryTime = 0.0; ///< server clock
};

/** The server has the whole file now: it can be downloaded. */
struct FileAvailable
{
    juce::MemoryBlock hash;
};

struct SessionEnded
{
    enum class Reason : std::uint8_t
    {
        removed,
        closed
    };

    Reason reason = Reason::closed;
};

// Presence and clocks (unreliable: lost ones do not matter) ------------------------------

struct Presence
{
    model::ObjectId track = 0; ///< the track looked at (0: none)
    bool hasCursor = false;
    model::ObjectId cursorTrack = 0;
    model::Ticks cursorPosition = 0;
    model::ObjectId clip = 0; ///< the clip open in the piano roll (0: none)
    int cursorKey = -1;       ///< the key under the cursor there (-1: none)
    model::Ticks clipCursorPosition = 0;
    std::vector<model::ObjectId> selectedClips;
    std::vector<std::int32_t> selectedNotes;
    bool playing = false;
    model::Ticks playhead = 0;

    bool operator==(const Presence&) const = default;
};

/** A participant's presence; participant 0 when a client sends its own. */
struct PresenceUpdate
{
    ParticipantNumber participant = 0;
    Presence presence;
};

struct Ping
{
    double clientTime = 0.0;
};

struct Pong
{
    double clientTime = 0.0;
    double serverTime = 0.0;
};

/** Every message inside the channel. The index of each type is its tag on the wire:
    append new types at the end. */
using Message =
    std::variant<ProjectUpload, Transaction, LockRequest, RecordingState, TransportRequest,
                 HostCommand, Leave, ProjectSnapshot, ParticipantList, LockState, AcceptedChange,
                 Rejection, TransportStart, Invite, FileAvailable, SessionEnded, PresenceUpdate,
                 Ping, Pong>;

/** Messages that may be lost (presence, clocks): sent outside the reliable order. */
[[nodiscard]] bool isUnreliable(const Message& message) noexcept;

[[nodiscard]] juce::MemoryBlock encode(const Message& message);
/** Nothing if the data is not a well-formed message (untrusted input). */
[[nodiscard]] std::optional<Message> decodeMessage(const juce::MemoryBlock& data);

/** Whole frames (with their FrameKind) for the handshake. */
[[nodiscard]] juce::MemoryBlock encodeFrame(const Hello& hello);
[[nodiscard]] juce::MemoryBlock encodeFrame(const Welcome& welcome);
[[nodiscard]] juce::MemoryBlock encodeFrame(const Refused& refused);
[[nodiscard]] std::optional<FrameKind> frameKindOf(const juce::MemoryBlock& frame);
[[nodiscard]] std::optional<Hello> decodeHello(const juce::MemoryBlock& frame);
[[nodiscard]] std::optional<Welcome> decodeWelcome(const juce::MemoryBlock& frame);
[[nodiscard]] std::optional<Refused> decodeRefused(const juce::MemoryBlock& frame);

/** What a hello says before anything that may change between versions: enough to refuse a
    peer of another version with a clear reason. Nothing if it is not a hello at all. */
struct HelloVersions
{
    std::uint32_t protocolVersion = 0;
    std::uint32_t formatVersion = 0;
};
[[nodiscard]] std::optional<HelloVersions> readHelloVersions(const juce::MemoryBlock& frame);

/** A channel frame around a payload, and the payload of one. */
[[nodiscard]] juce::MemoryBlock channelFrame(const juce::MemoryBlock& payload);
[[nodiscard]] juce::MemoryBlock channelPayload(const juce::MemoryBlock& frame);

/** Why a peer refused, written for the user. */
[[nodiscard]] juce::String describe(const Refused& refused);

} // namespace daw::coop
