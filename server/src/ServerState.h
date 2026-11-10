#pragma once

#include "daw/coop/Messages.h"
#include "daw/net/Link.h"
#include "daw/net/ReliableChannel.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <map>
#include <memory>
#include <vector>

namespace daw::server
{

struct Participant
{
    coop::ParticipantNumber number = 0;
    juce::String name;
    int colour = 0;
    juce::String tokenHash; ///< SHA-256 of the token, never the token itself
    bool host = false;
    bool gone = false;    ///< left or removed: the token works no more
    bool removed = false; ///< by the host
    std::uint64_t lastTransaction = 0;
    bool recording = false;

    // While the server runs (not saved):
    std::uint64_t connection = 0; ///< 0: not connected
    double disconnectedAt = 0.0;
    net::ReliableChannel channel;
    bool channelValid = false; ///< false: the next rejoin gets a snapshot
    coop::Presence presence;
    bool presencePending = false;
    double presenceSentAt = -1.0e9;
};

struct Lock
{
    coop::ParticipantNumber holder = 0;
    double lastUsed = 0.0;
};

struct Session
{
    juce::String id;
    juce::ValueTree project; ///< the shared form
    std::uint64_t sequence = 0;
    bool ready = false;
    std::map<coop::ParticipantNumber, Participant> participants;
    std::map<model::EditScope, Lock> locks;
    std::map<juce::String, coop::FileEntry> files; ///< by path
    juce::String inviteCode;
    double inviteExpiry = 0.0;
    double lastActive = 0.0;

    bool dirty = false;
    double savedAt = -1.0e9;
    std::vector<coop::AcceptedChange> journal;
    bool ended = false;
    double endAt = 0.0;
};

struct Connection
{
    std::unique_ptr<net::Link> link;
    juce::String address;
    double openedAt = 0.0;
    juce::String session; ///< empty until welcomed
    coop::ParticipantNumber participant = 0;
    bool files = false; ///< a participant's second connection, for files
    bool closing = false;
    double closeAt = 0.0;
};

} // namespace daw::server
