#pragma once

#include "ServerState.h"

#include <functional>
#include <set>
#include <vector>

namespace daw::server
{

/**
    Sessions on disk, so that a server that restarts loses nothing: for each session a
    folder (sessions/<id>) with session.json (participants, with their tokens hashed, the
    invite, the file list, the sequence), project.bin (the project, exact) and journal.bin
    (the transactions accepted since project.bin was written, appended as they are
    accepted). Files are written through temporary files, so a crash leaves the old one.
*/
class SessionStore
{
public:
    explicit SessionStore(juce::File folder);

    [[nodiscard]] std::vector<Session> loadAll(const std::function<void(const juce::String&)>& log);
    [[nodiscard]] bool save(Session& session);
    void append(const Session& session, const coop::AcceptedChange& change);
    void remove(const juce::String& sessionId);

    [[nodiscard]] std::set<juce::String> loadClosedSessions() const;
    void saveClosedSessions(const std::set<juce::String>& ids) const;

    [[nodiscard]] static juce::String hashOf(const juce::String& token);

private:
    [[nodiscard]] juce::File folderOf(const juce::String& sessionId) const;

    juce::File root;
};

} // namespace daw::server
