#pragma once

#include "daw/coop/Messages.h"
#include "daw/net/Link.h"
#include "daw/net/Tls.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace daw::coop
{

/**
    The audio files of a participant's copy (recordings, imports, samples): its own go up to
    the session's server, those of others come down, over a connection of their own so that
    edits never wait behind them.

    A file is known by the SHA-256 of its content: its own are hashed before anything refers
    to them (addLocalFile(), entriesFor()); a file of others is downloaded once, whatever
    paths name it (a second path gets a local copy), to "<name>.part", checked against its
    hash, then put in place (takeArrived()). Both directions resume where they stopped:
    uploads from what the server says it has, downloads from the part on disk. Work happens
    a piece at a time in update() (at most bytesPerUpdate read, written or hashed), on the
    thread that calls it.
*/
class FileSync
{
public:
    using LinkFactory = std::function<std::unique_ptr<net::Link>()>;

    struct Options
    {
        std::size_t chunkSize = 64 * 1024;
        std::uint64_t window = 1024 * 1024;           ///< bytes in flight each way
        std::uint64_t bytesPerUpdate = 4 * 1024 * 1024; ///< read, written or hashed per update()
        double firstRetry = 1.0;
        double longestRetry = 10.0;
    };

    struct State
    {
        enum class Kind
        {
            present,     ///< here and checked
            hashing,     ///< one's own, being hashed
            uploading,   ///< one's own, on its way to the server
            incoming,    ///< someone else's, not on the server yet
            downloading, ///< coming
            failed       ///< refused (too large for the session) or never right
        };

        Kind kind = Kind::present;
        std::uint64_t done = 0;
        std::uint64_t size = 0;
    };

    FileSync(juce::File copyFolder, LinkFactory linkFactory, Options options);
    ~FileSync();

    FileSync(const FileSync&) = delete;
    FileSync& operator=(const FileSync&) = delete;

    /** The session's credentials: from now on the connection is made when there is work. */
    void attach(const juce::String& sessionId, ParticipantNumber participant,
                const juce::String& token);
    void detach();

    void update(double now);

    /** Files the session's project refers to (from a snapshot or others' transactions). */
    void addSessionFiles(const std::vector<FileEntry>& entries);
    /** The server has this file whole now. */
    void fileAvailable(const juce::MemoryBlock& hash);

    /** One of this participant's files, already in the copy at a path relative to it
        ("Audio/2/take 1.wav"): it is hashed, then uploaded once attached. */
    void addLocalFile(const juce::String& path);

    /** The entries of paths whose hash is known (own files hashed, others' files). */
    [[nodiscard]] std::vector<FileEntry> entriesFor(const juce::StringArray& paths) const;
    /** True while any own file is still being hashed. */
    [[nodiscard]] bool isHashing() const;

    [[nodiscard]] std::optional<State> stateOf(const juce::String& path) const;
    /** Paths that became present since the last call (downloads put in place). */
    [[nodiscard]] std::vector<juce::String> takeArrived();
    /** Notices for the user (a file refused, a file that never matched its hash). */
    [[nodiscard]] juce::StringArray takeNotices();

    /** True when nothing waits to go up or come down. */
    [[nodiscard]] bool isIdle() const;

    /** Bytes sent and received on the files connection (benchmarks). */
    [[nodiscard]] std::uint64_t getBytesSent() const noexcept { return bytesSent; }
    [[nodiscard]] std::uint64_t getBytesReceived() const noexcept { return bytesReceived; }

private:
    struct Entry;

    void connect();
    void linkLost();
    void handle(const FileMessage& message);
    void send(const FileMessage& message);
    void hashSome(std::uint64_t& budget);
    void uploadSome(std::uint64_t& budget);
    void downloadSome();
    void startFileWork(Entry& entry);
    [[nodiscard]] Entry* findByHash(const juce::MemoryBlock& hash, bool downloading);
    [[nodiscard]] juce::File fileOf(const juce::String& path) const;

    juce::File folder;
    LinkFactory makeLink;
    Options options;

    juce::String sessionId;
    ParticipantNumber participant = 0;
    juce::String token;
    bool attached = false;

    std::unique_ptr<net::Link> link;
    bool welcomed = false;
    double now = 0.0;
    double connectAt = 0.0;
    double retryDelay = 1.0;

    std::map<juce::String, std::unique_ptr<Entry>> entries; ///< by path
    std::vector<juce::String> arrived;
    juce::StringArray notices;
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesReceived = 0;
};

} // namespace daw::coop
