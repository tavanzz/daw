#pragma once

#include "daw/coop/Messages.h"
#include "daw/net/Link.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::server
{

struct ServerOptions
{
    /** Keys that may open sessions (whoever runs the server hands them to friends). */
    juce::StringArray hostKeys;

    int maximumSessions = 20;
    int maximumParticipants = 8; ///< per session, those who have not left
    double inviteLifetime = 24.0 * 3600.0;
    double lockIdleTimeout = 30.0;   ///< a lock nobody used for this long goes
    double disconnectGrace = 30.0;   ///< the locks of someone who dropped out wait this long
    double helloTimeout = 10.0;      ///< a connection that says nothing is closed
    double abandonedLifetime = 30.0 * 86400.0; ///< a session nobody joins goes
    int failedAttemptsAllowed = 10;  ///< wrong codes, keys or tokens from one address...
    double failedAttemptsWindow = 600.0; ///< ...within this many seconds...
    double blockDuration = 900.0;        ///< ...block it for this long
    double presenceInterval = 0.05;      ///< presence passed on at most 20 times a second
    double transportLead = 0.3;          ///< "Play for Everyone" starts this much later
    double saveInterval = 5.0;           ///< sessions changed are written at most this often
    std::uint64_t maximumSessionBytes = std::uint64_t{20} << 30; ///< files a session keeps
    std::uint64_t maximumTotalBytes = std::uint64_t{100} << 30;  ///< files of all sessions
    std::uint64_t maximumFileBytes = std::uint64_t{4} << 30;     ///< one file

    /** Where sessions are kept between runs; none: in memory only (tests). */
    juce::File dataFolder;

    /** Lines for the server's log: ids, numbers, sizes, reasons, never names or contents. */
    std::function<void(const juce::String&)> log;
};

/**
    The co-op server's logic: sessions, participants, their one order of transactions,
    locks, invites, presence and the shared transport, over links it is given (TLS
    connections in the server program, a simulated network in tests). Deterministic: time
    comes from the caller (seconds; the program passes the wall clock, so that expiry
    survives restarts).

    Each session keeps its project in the shared form and accepts a transaction only if
    every change in it finds what it expects (model::applyChanges()), keeps the project
    well formed (model::checkChange()), and needs only locks that are free or the author's;
    the author then holds them. Accepted transactions go to every participant, the author
    included, in one order.

    Single-threaded: one loop calls everything.
*/
class Server
{
public:
    explicit Server(ServerOptions options);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /** A new connection (the peer's address, for the limit on failed attempts only). */
    void addConnection(std::unique_ptr<net::Link> link, const juce::String& address, double now);

    /** Reads what came in, deals with it, sends what is due, handles timeouts, saves. */
    void update(double now);

    /** Writes every changed session now (before quitting). */
    void saveAll();

    [[nodiscard]] int getNumSessions() const;
    [[nodiscard]] int getNumConnections() const;

    /** For tests: a session's project (shared form), its sequence and who holds what. */
    [[nodiscard]] juce::ValueTree getProject(const juce::String& sessionId) const;
    [[nodiscard]] std::uint64_t getSequence(const juce::String& sessionId) const;
    [[nodiscard]] std::vector<std::pair<model::EditScope, coop::ParticipantNumber>>
    getLocks(const juce::String& sessionId) const;
    [[nodiscard]] juce::StringArray getSessionIds() const;
    /** Bytes of files a session keeps. */
    [[nodiscard]] std::uint64_t getFileBytes(const juce::String& sessionId) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

/** An invite code as people type it: upper case, without dashes or spaces, O as 0, I and L
    as 1 (Crockford's Base32). */
[[nodiscard]] juce::String normaliseInviteCode(const juce::String& code);

} // namespace daw::server
