#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace daw::coop
{

/** Where a co-op server is: its address and the fingerprint of its key (pinned). */
struct ServerAddress
{
    juce::String host;
    int port = 0;
    juce::String fingerprint;

    bool operator==(const ServerAddress&) const = default;
};

/** An invite: the server and a session's code, and the project's name for the folder of
    the copy (optional). As text:
    micinstation://join?server=<host:port>&key=<fingerprint>&code=<code>&title=<name>. */
struct JoinLink
{
    ServerAddress server;
    juce::String code;
    juce::String title;
};

/** What lets someone open sessions on a server, handed out by whoever runs it. As text:
    micinstation://server?server=<host:port>&key=<fingerprint>&host=<host key>. */
struct HostLink
{
    ServerAddress server;
    juce::String hostKey;
};

[[nodiscard]] juce::String toText(const JoinLink& link);
[[nodiscard]] juce::String toText(const HostLink& link);

/** Reads a link as people paste it (spaces and line breaks around it do not matter);
    nothing if it is not one. */
[[nodiscard]] std::optional<JoinLink> parseJoinLink(const juce::String& text);
[[nodiscard]] std::optional<HostLink> parseHostLink(const juce::String& text);

/** "host:port", with an IPv6 address in brackets. */
[[nodiscard]] juce::String addressText(const ServerAddress& server);

} // namespace daw::coop
