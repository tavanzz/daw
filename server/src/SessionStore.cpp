#include "SessionStore.h"

#include "daw/coop/Wire.h"
#include "daw/model/ProjectChanges.h"
#include "daw/net/Tls.h"

namespace daw::server
{

namespace
{

constexpr const char* sessionFileName = "session.json";
constexpr const char* projectFileName = "project.bin";
constexpr const char* journalFileName = "journal.bin";
constexpr const char* closedFileName = "closed-sessions.txt";
constexpr std::size_t maximumClosedSessions = 1000;

bool isSessionId(const juce::String& text)
{
    return text.length() == 32 && text.containsOnly("0123456789abcdef");
}

bool writeAtomically(const juce::File& file, const juce::MemoryBlock& data)
{
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (stream.failedToOpen() || !stream.write(data.getData(), data.getSize()))
            return false;
        stream.flush();
        if (stream.getStatus().failed())
            return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}

juce::var toJson(const coop::FileEntry& file)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("path", file.path);
    object->setProperty("hash", juce::String::toHexString(file.hash.getData(),
                                                          static_cast<int>(file.hash.getSize()), 0));
    object->setProperty("size", static_cast<juce::int64>(file.size));
    return object;
}

std::optional<coop::FileEntry> fileFromJson(const juce::var& json)
{
    coop::FileEntry file;
    file.path = json["path"].toString();
    juce::MemoryBlock hash;
    hash.loadFromHexString(json["hash"].toString());
    file.hash = hash;
    file.size = static_cast<std::uint64_t>(static_cast<juce::int64>(json["size"]));
    if (file.hash.getSize() != 32 || !coop::isSafeSharedPath(file.path))
        return std::nullopt;
    return file;
}

juce::var participantToJson(const Participant& participant)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("number", static_cast<juce::int64>(participant.number));
    object->setProperty("name", participant.name);
    object->setProperty("colour", participant.colour);
    object->setProperty("tokenHash", participant.tokenHash);
    object->setProperty("host", participant.host);
    object->setProperty("gone", participant.gone);
    object->setProperty("removed", participant.removed);
    object->setProperty("lastTransaction", static_cast<juce::int64>(participant.lastTransaction));
    return object;
}

} // namespace

SessionStore::SessionStore(juce::File folder)
    : root(std::move(folder))
{
    root.getChildFile("sessions").createDirectory();
}

juce::String SessionStore::hashOf(const juce::String& token)
{
    const auto hash = net::sha256(token.toRawUTF8(), token.getNumBytesAsUTF8());
    return juce::String::toHexString(hash.getData(), static_cast<int>(hash.getSize()), 0);
}

juce::File SessionStore::folderOf(const juce::String& sessionId) const
{
    jassert(isSessionId(sessionId));
    return root.getChildFile("sessions").getChildFile(sessionId);
}

bool SessionStore::save(Session& session)
{
    const auto folder = folderOf(session.id);
    if (!folder.createDirectory())
        return false;

    coop::Writer project;
    project.writeVarUInt(session.sequence);
    project.writeBool(session.ready);
    project.writeTree(session.project.isValid() ? session.project : juce::ValueTree("Project"));
    if (!writeAtomically(folder.getChildFile(projectFileName), project.getData()))
        return false;

    auto* json = new juce::DynamicObject();
    const juce::var document(json);
    json->setProperty("id", session.id);
    json->setProperty("inviteCode", session.inviteCode);
    json->setProperty("inviteExpiry", session.inviteExpiry);
    json->setProperty("lastActive", session.lastActive);
    juce::Array<juce::var> participants;
    for (const auto& [number, participant] : session.participants)
        participants.add(participantToJson(participant));
    json->setProperty("participants", participants);
    juce::Array<juce::var> files;
    for (const auto& [path, file] : session.files)
        files.add(toJson(file));
    json->setProperty("files", files);

    const auto text = juce::JSON::toString(document);
    if (!writeAtomically(folder.getChildFile(sessionFileName),
                         juce::MemoryBlock(text.toRawUTF8(), text.getNumBytesAsUTF8())))
        return false;

    // Everything the journal held is in project.bin now.
    folder.getChildFile(journalFileName).deleteFile();
    session.journal.clear();
    return true;
}

void SessionStore::append(const Session& session, const coop::AcceptedChange& change)
{
    const auto folder = folderOf(session.id);
    folder.createDirectory();
    juce::FileOutputStream stream(folder.getChildFile(journalFileName));
    if (stream.failedToOpen())
        return;
    const auto encoded = coop::encode(change);
    stream.writeInt(static_cast<int>(encoded.getSize()));
    stream.write(encoded.getData(), encoded.getSize());
    stream.flush();
}

void SessionStore::remove(const juce::String& sessionId)
{
    if (isSessionId(sessionId))
        folderOf(sessionId).deleteRecursively();
}

std::vector<Session> SessionStore::loadAll(const std::function<void(const juce::String&)>& log)
{
    std::vector<Session> sessions;

    for (const auto& entry : juce::RangedDirectoryIterator(root.getChildFile("sessions"), false, "*",
                                                           juce::File::findDirectories))
    {
        const auto folder = entry.getFile();
        const auto id = folder.getFileName();
        if (!isSessionId(id))
            continue;

        const auto json = juce::JSON::parse(folder.getChildFile(sessionFileName));
        juce::MemoryBlock projectData;
        if (!json.isObject() || !folder.getChildFile(projectFileName).loadFileAsData(projectData))
        {
            log("session " + id.substring(0, 8) + " could not be read: left out");
            continue;
        }

        Session session;
        session.id = id;
        session.inviteCode = json["inviteCode"].toString();
        session.inviteExpiry = static_cast<double>(json["inviteExpiry"]);
        session.lastActive = static_cast<double>(json["lastActive"]);

        if (const auto* participants = json["participants"].getArray())
            for (const auto& item : *participants)
            {
                Participant participant;
                participant.number = static_cast<coop::ParticipantNumber>(
                    static_cast<juce::int64>(item["number"]));
                participant.name = item["name"].toString();
                participant.colour = juce::jlimit(0, 7, static_cast<int>(item["colour"]));
                participant.tokenHash = item["tokenHash"].toString();
                participant.host = static_cast<bool>(item["host"]);
                participant.gone = static_cast<bool>(item["gone"]);
                participant.removed = static_cast<bool>(item["removed"]);
                participant.lastTransaction = static_cast<std::uint64_t>(
                    static_cast<juce::int64>(item["lastTransaction"]));
                if (participant.number > 0)
                    session.participants.emplace(participant.number, std::move(participant));
            }

        if (const auto* files = json["files"].getArray())
            for (const auto& item : *files)
                if (const auto file = fileFromJson(item))
                    session.files[file->path] = *file;

        coop::Reader reader(projectData);
        session.sequence = reader.readVarUInt();
        session.ready = reader.readBool();
        session.project = reader.readTree();
        if (!reader.isOk() || session.participants.empty())
        {
            log("session " + id.substring(0, 8) + " could not be read: left out");
            continue;
        }

        // Transactions accepted after project.bin was written.
        juce::MemoryBlock journal;
        if (folder.getChildFile(journalFileName).loadFileAsData(journal))
        {
            juce::MemoryInputStream stream(journal, false);
            while (stream.getNumBytesRemaining() >= 4)
            {
                const auto size = stream.readInt();
                if (size <= 0 || size > stream.getNumBytesRemaining())
                    break;
                juce::MemoryBlock encoded;
                stream.readIntoMemoryBlock(encoded, size);
                const auto message = coop::decodeMessage(encoded);
                const auto* change = message ? std::get_if<coop::AcceptedChange>(&*message) : nullptr;
                if (change == nullptr)
                    break;

                if (const auto author = session.participants.find(change->author);
                    author != session.participants.end())
                    author->second.lastTransaction =
                        std::max(author->second.lastTransaction, change->clientSequence);
                if (change->sequence <= session.sequence)
                    continue;
                if (change->sequence != session.sequence + 1
                    || model::applyChanges(session.project, change->changes).failed())
                {
                    log("session " + id.substring(0, 8) + ": its journal stops at "
                        + juce::String(session.sequence));
                    break;
                }
                for (const auto& file : change->files)
                    session.files[file.path] = file;
                session.sequence = change->sequence;
            }
        }

        session.dirty = true; // project.bin with the journal in it
        sessions.push_back(std::move(session));
    }

    return sessions;
}

std::set<juce::String> SessionStore::loadClosedSessions() const
{
    std::set<juce::String> ids;
    juce::StringArray lines;
    lines.addLines(root.getChildFile(closedFileName).loadFileAsString());
    for (const auto& line : lines)
        if (isSessionId(line.trim()))
            ids.insert(line.trim());
    return ids;
}

void SessionStore::saveClosedSessions(const std::set<juce::String>& ids) const
{
    juce::String text;
    std::size_t count = 0;
    for (auto id = ids.rbegin(); id != ids.rend() && count < maximumClosedSessions; ++id, ++count)
        text << *id << "\n";
    root.getChildFile(closedFileName).replaceWithText(text, false, false, "\n");
}

} // namespace daw::server
