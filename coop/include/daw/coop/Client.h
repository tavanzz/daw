#pragma once

#include "daw/coop/FileSync.h"
#include "daw/coop/Messages.h"
#include "daw/model/ChangeRecorder.h"
#include "daw/net/Link.h"
#include "daw/net/ReliableChannel.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace daw::coop
{

/**
    A participant's side of a co-op session: keeps the local copy of the project in step
    with everyone else's through the server, for any UI (desktop now, mobile later).

    It records the project's changes (model::ChangeRecorder) and sends them, a transaction
    at a time (flush()), as soon as they are made: the copy shows them at once. The server
    puts every transaction of the session in one order; changes of others are applied to the
    copy as they come (with the recorder off), on the message thread, so that they reach the
    engine the way local edits do. A transaction the server refuses (someone took the lock
    first, or the project was not what it expected) is undone in the copy, with a notice;
    where a change of someone else touches what is still waiting for the server, the
    waiting changes are taken back, the other change applied, and the waiting ones applied
    again (those that no longer fit are dropped when the server refuses them).

    Undo is personal: undo() reverts this participant's last undo step, as a new
    transaction, if what it changed is still as it left it and nobody else holds its locks.

    The connection is made, kept and made again by the client (links come from a factory):
    after a drop it rejoins and the reliable channel sends what each side missed, or the
    server sends the whole project. Messages are handled in update(), called regularly from
    the thread that owns the project (the app's message thread; tests' loop).

    Locks: mayEdit() says whether this participant may change something now: its scope is
    free or theirs, and the session is connected (or reconnecting for less than
    readOnlyAfter, for scopes it holds).
*/
class Client
{
public:
    using LinkFactory = std::function<std::unique_ptr<net::Link>()>;

    struct Options
    {
        juce::String appVersion;
        double readOnlyAfter = 30.0;     ///< seconds without a connection: nothing may change
        double firstRetry = 1.0;         ///< seconds before connecting again, doubling...
        double longestRetry = 10.0;      ///< ...up to this
        double helloTimeout = 10.0;      ///< no answer to a hello: connect again
        double pingInterval = 2.0;
        double presenceInterval = 0.05;  ///< at most 20 presence updates a second
        double lockRefreshInterval = 10.0;
        FileSync::Options files;
    };

    enum class Status
    {
        idle,         ///< not in a session
        connecting,   ///< first connection
        connected,
        reconnecting, ///< dropped out; trying again
        ended         ///< refused, removed or closed: see getProblem()
    };

    /** A notice for the user, about one of their edits or the session. */
    struct Notice
    {
        juce::String text;
    };

    /** The project the client keeps in step, and the folder of its copy (file paths in the
        shared form are relative to it). */
    Client(juce::ValueTree projectState, juce::File copyFolder, LinkFactory linkFactory,
           Options options);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Starting -------------------------------------------------------------------------

    /** Opens a session with this project (the host). */
    void host(const juce::String& hostKey, const juce::String& name);
    /** Joins a session with an invite code: the project is replaced by the session's. */
    void join(const juce::String& inviteCode, const juce::String& name);
    /** Comes back to a session this copy was part of (after the app started again). */
    void rejoin(const juce::String& sessionId, ParticipantNumber participant,
                const juce::String& token);

    /** Leaves for good (the token stops working). */
    void leave();

    // The loop -------------------------------------------------------------------------

    /** Sends what was recorded, deals with what came in, keeps the connection. */
    void update(double now);

    // Local edits ----------------------------------------------------------------------

    /** What changes recorded from now on are: their name ("Move Clip") and the undo step
        they belong to (0: not undoable). Sends what was recorded before as its own. */
    void setTransaction(const juce::String& name, std::uint64_t undoStep);
    /** A new undo step id. */
    [[nodiscard]] std::uint64_t newUndoStep() noexcept { return ++lastUndoStep; }

    /** Sends the changes recorded so far as one transaction. A change in a scope someone
        else holds (or while the copy is read-only) is undone at once, with a notice. */
    void flush();

    /** At most one transaction this often from update() (a drag in progress: 0.1 s), so
        that a gesture is a few transactions, not one per mouse move; 0: every update. */
    void setFlushInterval(double seconds) noexcept { flushInterval = seconds; }

    [[nodiscard]] bool mayEdit(const model::EditScope& scope) const;
    /** Who holds a scope (0: nobody). */
    [[nodiscard]] ParticipantNumber holderOf(const model::EditScope& scope) const;

    /** Keeps these scopes locked while it is called again within lockRefreshInterval (a
        drag in progress, armed tracks while recording); others are let go when unused. */
    void holdLocks(const std::vector<model::EditScope>& scopes);
    void releaseLock(const model::EditScope& scope);

    // Personal undo --------------------------------------------------------------------

    [[nodiscard]] bool canUndo() const noexcept { return !history.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redoHistory.empty(); }
    [[nodiscard]] juce::String getUndoName() const;
    [[nodiscard]] juce::String getRedoName() const;
    /** False (with a notice) if it could not be undone; the step is then forgotten unless
        only a lock was in the way. */
    bool undo();
    bool redo();

    // The session ----------------------------------------------------------------------

    [[nodiscard]] Status getStatus() const noexcept { return status; }
    [[nodiscard]] juce::String getProblem() const { return problem; }
    [[nodiscard]] juce::String getSessionId() const { return sessionId; }
    [[nodiscard]] ParticipantNumber getParticipant() const noexcept { return participant; }
    [[nodiscard]] juce::String getToken() const { return token; }
    [[nodiscard]] bool isHost() const;
    [[nodiscard]] const std::vector<ParticipantInfo>& getParticipants() const noexcept
    {
        return participants;
    }
    [[nodiscard]] const ParticipantInfo* findParticipant(ParticipantNumber number) const;
    [[nodiscard]] std::optional<Invite> getInvite() const { return invite; }
    /** The sequence of the last transaction of the session this copy has. */
    [[nodiscard]] std::uint64_t getSequence() const noexcept { return sequence; }
    /** Transactions sent and not yet accepted or refused. */
    [[nodiscard]] std::size_t getNumPending() const noexcept { return pending.size(); }
    /** Goes up whenever anything a view shows about the session changed. */
    [[nodiscard]] std::uint64_t getRevision() const noexcept { return revision; }

    [[nodiscard]] std::vector<Notice> takeNotices();

    /** The copy's audio files: one's own go up (add them with addLocalFile() and let them
        be hashed before anything refers to them), those of others come down. Transactions
        and the host's upload carry the entries of the files they start to refer to. */
    [[nodiscard]] FileSync& getFiles() noexcept { return files; }
    [[nodiscard]] const FileSync& getFiles() const noexcept { return files; }

    // Host -----------------------------------------------------------------------------

    void sendHostCommand(HostCommand command);

    // Presence, recording, transport and clocks ----------------------------------------

    void setPresence(const Presence& presence);
    [[nodiscard]] const std::map<ParticipantNumber, Presence>& getPresences() const noexcept
    {
        return presences;
    }

    void setRecording(bool recording);

    void requestTransport(bool play, model::Ticks position);
    [[nodiscard]] std::vector<TransportStart> takeTransportStarts();

    /** The server's clock at local time `localTime`, as well as it is known (needs a
        connection first). */
    [[nodiscard]] std::optional<double> serverTime(double localTime) const;
    /** How far off serverTime() may be: half the shortest round trip measured. */
    [[nodiscard]] std::optional<double> clockUncertainty() const;
    /** The local time at which the server's clock shows serverTime (for a TransportStart). */
    [[nodiscard]] std::optional<double> localTime(double serverTimeToConvert) const;

private:
    struct Pending
    {
        std::uint64_t clientSequence = 0;
        juce::String name;
        std::uint64_t undoStep = 0;
        model::ChangeSet changes; ///< local form, as applied to the copy
        std::vector<model::EditScope> scopes;
        bool applied = true; ///< false: taken back and not applicable any more
        enum class Kind : std::uint8_t
        {
            edit,
            undo,
            redo
        } kind = Kind::edit;
    };

    struct UndoStep
    {
        std::uint64_t id = 0;
        juce::String name;
        model::ChangeSet changes; ///< local form
    };

    void connect();
    void handleFrame(const juce::MemoryBlock& frame);
    void handleWelcome(const Welcome& welcome);
    void handleMessage(const Message& message);
    void handleSnapshot(const ProjectSnapshot& snapshot);
    void handleAccepted(const AcceptedChange& change);
    void handleRejection(const Rejection& rejection);
    void uploadProject();
    void linkLost(const juce::String& reason);
    void end(const juce::String& reason);
    void send(const Message& message);
    void sendPending(const Pending& transaction);
    void submit(Pending transaction);

    /** checked: each change must also keep the project well formed (model::checkChange()),
        for this participant's own changes applied again on top of others'. */
    bool applyToCopy(const model::ChangeSet& changes, bool checked = false);
    void takeBackPending(std::size_t from);
    void applyPendingAgain(std::size_t from);
    void notify(const juce::String& text);
    [[nodiscard]] juce::String nameOf(ParticipantNumber number) const;
    [[nodiscard]] juce::String describeScope(const model::EditScope& scope) const;
    void changed() noexcept { ++revision; }

    juce::ValueTree state;
    juce::File folder;
    LinkFactory makeLink;
    Options options;
    model::ChangeRecorder recorder;

    enum class Request
    {
        none,
        create,
        join,
        rejoin
    } request = Request::none;
    juce::String hostKey, inviteCode, name;
    bool uploaded = false;
    bool synced = false; ///< the copy is in step with the session's project

    std::unique_ptr<net::Link> link;
    bool welcomed = false;
    std::vector<juce::MemoryBlock> earlyFrames;
    net::ReliableChannel channel;
    Status status = Status::idle;
    juce::String problem;
    double now = 0.0;
    double connectAt = 0.0;
    double retryDelay = 1.0;
    double helloSentAt = 0.0;
    double disconnectedSince = 0.0;

    juce::String sessionId;
    ParticipantNumber participant = 0;
    juce::String token;
    std::uint64_t sequence = 0;
    std::vector<ParticipantInfo> participants;
    std::map<model::EditScope, ParticipantNumber> locks;
    std::map<model::EditScope, double> heldLocks; ///< asked to keep, last asked
    std::optional<Invite> invite;
    std::map<ParticipantNumber, Presence> presences;
    std::vector<TransportStart> transportStarts;
    FileSync files;
    bool uploadWaiting = false; ///< the host's project goes once its files are hashed

    double flushInterval = 0.0;
    double flushedAt = -1.0e9;
    juce::String transactionName;
    std::uint64_t transactionStep = 0;
    std::uint64_t lastUndoStep = 0;
    std::uint64_t lastClientSequence = 0;
    std::deque<Pending> pending;
    std::vector<UndoStep> history;
    std::vector<UndoStep> redoHistory;

    std::optional<Presence> presenceToSend;
    Presence lastPresenceSent;
    double presenceSentAt = -1.0e9;
    double pingSentAt = -1.0e9;
    struct ClockSample
    {
        double roundTrip = 0.0;
        double offset = 0.0;
    };
    std::deque<ClockSample> clockSamples;

    std::vector<Notice> notices;
    std::uint64_t revision = 0;
};

} // namespace daw::coop
