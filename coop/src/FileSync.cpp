#include "daw/coop/FileSync.h"

#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"

#include <algorithm>

namespace daw::coop
{

struct FileSync::Entry
{
    juce::String path;
    juce::MemoryBlock hash; ///< empty until known
    std::uint64_t size = 0;
    bool own = false;
    State::Kind kind = State::Kind::present;

    // Hashing (own files, and files of others already on disk, to check them).
    std::unique_ptr<juce::FileInputStream> reading;
    std::unique_ptr<net::Sha256> hasher;
    std::uint64_t hashed = 0;

    // Uploading.
    bool queried = false;
    bool statusKnown = false;
    std::uint64_t acknowledged = 0;
    std::uint64_t sent = 0;
    double lastProgress = 0.0; ///< when the other side last moved on (both directions)

    // Downloading.
    bool requested = false;
    std::uint64_t received = 0;
    std::uint64_t requestedUpTo = 0;
    std::unique_ptr<net::Sha256> downloadHasher;
    int attempts = 0;
};

namespace
{

// Seconds without progress before a transfer starts again from what the other side has.
constexpr double stallTime = 1.0;

juce::File partOf(const juce::File& file)
{
    return file.getSiblingFile(file.getFileName() + ".part");
}

} // namespace

FileSync::FileSync(juce::File copyFolder, LinkFactory linkFactory, Options syncOptions)
    : folder(std::move(copyFolder)), makeLink(std::move(linkFactory)), options(syncOptions)
{
    retryDelay = options.firstRetry;
}

FileSync::~FileSync() = default;

juce::File FileSync::fileOf(const juce::String& path) const
{
    return folder.getChildFile(path);
}

void FileSync::attach(const juce::String& id, ParticipantNumber number, const juce::String& secret)
{
    if (attached && id == sessionId && number == participant)
        return;
    sessionId = id;
    participant = number;
    token = secret;
    attached = true;
    link.reset();
    welcomed = false;
    connectAt = now;
}

void FileSync::detach()
{
    attached = false;
    link.reset();
    welcomed = false;
}

// Files -------------------------------------------------------------------------------------

void FileSync::addLocalFile(const juce::String& path)
{
    if (!isSafeSharedPath(path))
    {
        jassertfalse;
        return;
    }
    auto entry = std::make_unique<Entry>();
    entry->path = path;
    entry->own = true;
    entry->kind = State::Kind::hashing;
    const auto file = fileOf(path);
    entry->size = static_cast<std::uint64_t>(std::max<juce::int64>(0, file.getSize()));
    entry->reading = std::make_unique<juce::FileInputStream>(file);
    entry->hasher = std::make_unique<net::Sha256>();
    entries[path] = std::move(entry);
}

void FileSync::addSessionFiles(const std::vector<FileEntry>& newEntries)
{
    for (const auto& file : newEntries)
    {
        if (!isSafeSharedPath(file.path) || file.hash.getSize() != 32)
            continue;
        if (const auto existing = entries.find(file.path);
            existing != entries.end() && existing->second->hash == file.hash)
            continue;

        auto entry = std::make_unique<Entry>();
        entry->path = file.path;
        entry->hash = file.hash;
        entry->size = file.size;

        const auto local = fileOf(file.path);
        if (local.existsAsFile() && static_cast<std::uint64_t>(local.getSize()) == file.size)
        {
            // Already here (an earlier session, or a copy kept): checked before it counts.
            entry->kind = State::Kind::hashing;
            entry->reading = std::make_unique<juce::FileInputStream>(local);
            entry->hasher = std::make_unique<net::Sha256>();
        }
        else
            startFileWork(*entry);
        entries[file.path] = std::move(entry);
    }
}

void FileSync::startFileWork(Entry& entry)
{
    // The same content under another path, here already: a local copy is enough.
    for (const auto& [path, other] : entries)
        if (other.get() != &entry && other->hash == entry.hash
            && other->kind == State::Kind::present && fileOf(path).existsAsFile())
        {
            const auto destination = fileOf(entry.path);
            destination.getParentDirectory().createDirectory();
            if (fileOf(path).copyFileTo(destination))
            {
                entry.kind = State::Kind::present;
                arrived.push_back(entry.path);
                return;
            }
        }

    entry.kind = State::Kind::downloading;
    const auto part = partOf(fileOf(entry.path));
    entry.received = 0;
    entry.downloadHasher = std::make_unique<net::Sha256>();
    if (part.existsAsFile())
    {
        // Resumed: what is there counts once hashed.
        juce::FileInputStream input(part);
        juce::HeapBlock<char> buffer(1 << 16);
        for (;;)
        {
            const auto read = input.read(buffer.get(), 1 << 16);
            if (read <= 0)
                break;
            entry.downloadHasher->add(buffer.get(), static_cast<std::size_t>(read));
            entry.received += static_cast<std::uint64_t>(read);
        }
        if (entry.received > entry.size)
        {
            part.deleteFile();
            entry.received = 0;
            entry.downloadHasher = std::make_unique<net::Sha256>();
        }
    }
    entry.requested = false;
    entry.requestedUpTo = entry.received;
}

void FileSync::fileAvailable(const juce::MemoryBlock& hash)
{
    for (auto& [path, entry] : entries)
        if (entry->hash == hash && entry->kind == State::Kind::incoming)
        {
            entry->kind = State::Kind::downloading;
            entry->requested = false;
            entry->requestedUpTo = entry->received;
        }
}

std::vector<FileEntry> FileSync::entriesFor(const juce::StringArray& paths) const
{
    std::vector<FileEntry> found;
    for (const auto& path : paths)
        if (const auto entry = entries.find(path);
            entry != entries.end() && !entry->second->hash.isEmpty())
            found.push_back({path, entry->second->hash, entry->second->size});
    return found;
}

bool FileSync::isHashing() const
{
    return std::any_of(entries.begin(), entries.end(),
                       [](const auto& entry)
                       { return entry.second->own && entry.second->kind == State::Kind::hashing; });
}

std::optional<FileSync::State> FileSync::stateOf(const juce::String& path) const
{
    const auto found = entries.find(path);
    if (found == entries.end())
        return std::nullopt;
    const auto& entry = *found->second;
    State state;
    state.kind = entry.kind;
    state.size = entry.size;
    switch (entry.kind)
    {
    case State::Kind::hashing:
        state.done = entry.hashed;
        break;
    case State::Kind::uploading:
        state.done = entry.acknowledged;
        break;
    case State::Kind::incoming:
    case State::Kind::downloading:
        state.done = entry.received;
        break;
    case State::Kind::present:
        state.done = entry.size;
        break;
    case State::Kind::failed:
        break;
    }
    return state;
}

std::vector<juce::String> FileSync::takeArrived()
{
    return std::exchange(arrived, {});
}

juce::StringArray FileSync::takeNotices()
{
    return std::exchange(notices, {});
}

bool FileSync::isIdle() const
{
    return std::all_of(entries.begin(), entries.end(),
                       [](const auto& entry)
                       {
                           return entry.second->kind == State::Kind::present
                                  || entry.second->kind == State::Kind::failed;
                       });
}

// The loop ----------------------------------------------------------------------------------

void FileSync::update(double time)
{
    now = time;
    auto budget = options.bytesPerUpdate;
    hashSome(budget);

    if (!attached)
        return;

    if (link == nullptr && now >= connectAt)
        connect();
    if (link == nullptr)
        return;

    for (const auto& frame : link->receive())
    {
        bytesReceived += frame.getSize();
        switch (frameKindOf(frame).value_or(FrameKind::hello))
        {
        case FrameKind::welcome:
            welcomed = true;
            retryDelay = options.firstRetry;
            // Everything starts again from what each side has.
            for (auto& [path, entry] : entries)
            {
                entry->queried = false;
                entry->statusKnown = false;
                entry->requested = false;
                entry->requestedUpTo = entry->received;
            }
            break;
        case FrameKind::refused:
            if (const auto refused = decodeRefused(frame))
                notices.add("Files cannot be exchanged: " + describe(*refused));
            detach();
            return;
        case FrameKind::files:
            if (const auto message = decodeFileFrame(frame))
                handle(*message);
            break;
        case FrameKind::hello:
        case FrameKind::channel:
            break;
        }
    }

    if (link != nullptr && link->getState() == net::Link::State::closed)
        return linkLost();
    if (!welcomed)
        return;

    uploadSome(budget);
    downloadSome();
}

void FileSync::connect()
{
    link = makeLink();
    welcomed = false;
    Hello hello;
    hello.protocolVersion = static_cast<std::uint32_t>(model::networkProtocolVersion);
    hello.formatVersion = static_cast<std::uint32_t>(model::currentFormatVersion);
    hello.request = AttachFiles{sessionId, participant, token};
    const auto frame = encodeFrame(hello);
    bytesSent += frame.getSize();
    link->send(frame);
}

void FileSync::linkLost()
{
    link.reset();
    welcomed = false;
    connectAt = now + retryDelay;
    retryDelay = std::min(retryDelay * 2.0, options.longestRetry);
}

void FileSync::send(const FileMessage& message)
{
    const auto frame = encodeFrame(message);
    bytesSent += frame.getSize();
    link->send(frame);
}

void FileSync::hashSome(std::uint64_t& budget)
{
    for (auto& [path, entry] : entries)
    {
        if (entry->kind != State::Kind::hashing || budget == 0)
            continue;

        juce::HeapBlock<char> buffer(1 << 16);
        while (budget > 0 && entry->hashed < entry->size)
        {
            const auto read = entry->reading->read(buffer.get(), 1 << 16);
            if (read <= 0)
                break;
            entry->hasher->add(buffer.get(), static_cast<std::size_t>(read));
            entry->hashed += static_cast<std::uint64_t>(read);
            budget -= std::min(budget, static_cast<std::uint64_t>(read));
        }
        if (entry->hashed < entry->size && !entry->reading->isExhausted())
            continue;

        const auto hash = entry->hasher->finish();
        entry->reading.reset();
        entry->hasher.reset();

        if (entry->own)
        {
            entry->hash = hash;
            entry->kind = State::Kind::uploading;
        }
        else if (hash == entry->hash)
        {
            entry->kind = State::Kind::present;
            arrived.push_back(path);
        }
        else
            startFileWork(*entry);
    }
}

void FileSync::uploadSome(std::uint64_t& budget)
{
    std::vector<juce::MemoryBlock> busy;
    for (auto& [path, entry] : entries)
    {
        if (!entry->own || entry->kind != State::Kind::uploading)
            continue;
        // One upload per content: the others with it are done when it is.
        if (std::find(busy.begin(), busy.end(), entry->hash) != busy.end())
            continue;
        busy.push_back(entry->hash);
        if (!entry->queried
            || (!entry->statusKnown && now - entry->lastProgress > stallTime))
        {
            send(UploadQuery{entry->hash, entry->size});
            entry->queried = true;
            entry->lastProgress = now;
            continue;
        }
        if (!entry->statusKnown)
            continue;
        if (entry->sent > entry->acknowledged && now - entry->lastProgress > stallTime)
        {
            // Chunks lost or out of order (the server takes them in order only): again from
            // what it has.
            entry->sent = entry->acknowledged;
            entry->lastProgress = now;
        }

        juce::FileInputStream input(fileOf(path));
        while (budget > 0 && entry->sent < entry->size
               && entry->sent - entry->acknowledged < options.window)
        {
            const auto length = std::min<std::uint64_t>(options.chunkSize, entry->size - entry->sent);
            juce::MemoryBlock data(static_cast<std::size_t>(length));
            if (input.failedToOpen() || !input.setPosition(static_cast<juce::int64>(entry->sent))
                || input.read(data.getData(), static_cast<int>(length)) != static_cast<int>(length))
            {
                entry->kind = State::Kind::failed;
                notices.add(path.fromLastOccurrenceOf("/", false, false)
                            + " could not be read to send it.");
                break;
            }
            send(UploadChunk{entry->hash, entry->sent, std::move(data)});
            entry->sent += length;
            budget -= std::min(budget, length);
        }
    }
}

void FileSync::downloadSome()
{
    std::vector<juce::MemoryBlock> busy;
    for (auto& [path, entry] : entries)
    {
        if (entry->kind != State::Kind::downloading)
            continue;
        // One download per content: other paths with it wait and copy it.
        if (std::find(busy.begin(), busy.end(), entry->hash) != busy.end())
            continue;
        busy.push_back(entry->hash);

        if (entry->requested && now - entry->lastProgress > stallTime)
        {
            // Nothing for a while: chunks lost or out of order. Again from what is here.
            entry->requested = false;
            entry->requestedUpTo = entry->received;
        }
        if (entry->requestedUpTo >= entry->size)
            continue;
        if (entry->requested && entry->received + options.window / 2 < entry->requestedUpTo)
            continue;
        if (!entry->requested)
            entry->lastProgress = now;

        const auto from = entry->requested ? entry->requestedUpTo : entry->received;
        send(DownloadRequest{entry->hash, from, options.window});
        entry->requested = true;
        entry->requestedUpTo = std::min(entry->size, from + options.window);
    }
}

FileSync::Entry* FileSync::findByHash(const juce::MemoryBlock& hash, bool downloading)
{
    for (auto& [path, entry] : entries)
        if (entry->hash == hash
            && (!downloading || entry->kind == State::Kind::downloading
                || entry->kind == State::Kind::incoming))
            return entry.get();
    return nullptr;
}

void FileSync::handle(const FileMessage& message)
{
    if (const auto* status = std::get_if<UploadStatus>(&message))
    {
        for (auto& [path, entry] : entries)
        {
            if (!entry->own || entry->hash != status->hash || entry->kind != State::Kind::uploading)
                continue;
            entry->statusKnown = true;
            if (status->stored > entry->acknowledged || !entry->queried)
                entry->lastProgress = now;
            entry->acknowledged = status->stored;
            if (entry->sent < status->stored || !entry->queried)
                entry->sent = status->stored;
            if (status->stored < entry->sent && entry->sent - status->stored > options.window)
                entry->sent = status->stored; // the server lost track: from what it has
            if (status->complete)
                entry->kind = State::Kind::present;
            else if (status->refused)
            {
                entry->kind = State::Kind::failed;
                notices.add(path.fromLastOccurrenceOf("/", false, false)
                            + " was not sent: it is larger than the session may keep.");
            }
        }
    }
    else if (const auto* chunk = std::get_if<DownloadChunk>(&message))
    {
        auto* entry = findByHash(chunk->hash, true);
        if (entry == nullptr || entry->kind != State::Kind::downloading || chunk->offset != entry->received
            || entry->received + chunk->data.getSize() > entry->size)
            return;

        const auto destination = fileOf(entry->path);
        const auto part = partOf(destination);
        part.getParentDirectory().createDirectory();
        {
            juce::FileOutputStream output(part);
            if (output.failedToOpen() || !output.write(chunk->data.getData(), chunk->data.getSize()))
                return;
        }
        entry->downloadHasher->add(chunk->data.getData(), chunk->data.getSize());
        entry->received += chunk->data.getSize();
        entry->lastProgress = now;
        if (entry->received < entry->size)
            return;

        if (entry->downloadHasher->finish() != entry->hash)
        {
            part.deleteFile();
            if (++entry->attempts >= 3)
            {
                entry->kind = State::Kind::failed;
                notices.add(entry->path.fromLastOccurrenceOf("/", false, false)
                            + " never arrived as it should.");
            }
            else
                startFileWork(*entry);
            return;
        }

        destination.deleteFile();
        if (!part.moveFileTo(destination))
        {
            entry->kind = State::Kind::failed;
            return;
        }
        entry->kind = State::Kind::present;
        arrived.push_back(entry->path);

        // Other paths with the same content.
        for (auto& [path, other] : entries)
            if (other.get() != entry && other->hash == entry->hash
                && (other->kind == State::Kind::downloading || other->kind == State::Kind::incoming))
            {
                partOf(fileOf(path)).deleteFile();
                startFileWork(*other);
            }
    }
    else if (const auto* unavailable = std::get_if<DownloadUnavailable>(&message))
    {
        for (auto& [path, entry] : entries)
            if (entry->hash == unavailable->hash && entry->kind == State::Kind::downloading)
            {
                entry->kind = State::Kind::incoming;
                entry->requested = false;
                entry->requestedUpTo = entry->received;
            }
    }
}

} // namespace daw::coop
