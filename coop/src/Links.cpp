#include "daw/coop/Links.h"

#include <map>

namespace daw::coop
{

namespace
{

std::optional<ServerAddress> serverFrom(const juce::String& address, const juce::String& fingerprint)
{
    const auto colon = address.lastIndexOfChar(':');
    if (colon <= 0 || fingerprint.length() != 43)
        return std::nullopt;
    ServerAddress server;
    server.host = address.substring(0, colon).trimCharactersAtStart("[").trimCharactersAtEnd("]");
    server.port = address.substring(colon + 1).getIntValue();
    server.fingerprint = fingerprint;
    if (server.host.isEmpty() || server.port <= 0 || server.port > 65535)
        return std::nullopt;
    return server;
}

// The parameters of "micinstation://<kind>?a=b&c=d".
std::optional<std::map<juce::String, juce::String>> linkParameters(const juce::String& text,
                                                                    const juce::String& kind)
{
    const auto link = text.trim();
    const auto prefix = "micinstation://" + kind + "?";
    if (!link.startsWithIgnoreCase(prefix))
        return std::nullopt;

    std::map<juce::String, juce::String> parameters;
    juce::StringArray pairs;
    pairs.addTokens(link.substring(prefix.length()), "&", "");
    for (const auto& pair : pairs)
        parameters[pair.upToFirstOccurrenceOf("=", false, false)] =
            juce::URL::removeEscapeChars(pair.fromFirstOccurrenceOf("=", false, false));
    return parameters;
}

juce::String parameter(const std::map<juce::String, juce::String>& parameters, const char* name)
{
    const auto found = parameters.find(name);
    return found != parameters.end() ? found->second : juce::String();
}

juce::String escaped(const juce::String& text)
{
    return juce::URL::addEscapeChars(text, true);
}

} // namespace

juce::String addressText(const ServerAddress& server)
{
    const auto host = server.host.containsChar(':') ? "[" + server.host + "]" : server.host;
    return host + ":" + juce::String(server.port);
}

juce::String toText(const JoinLink& link)
{
    auto text = "micinstation://join?server=" + escaped(addressText(link.server))
                + "&key=" + link.server.fingerprint + "&code=" + escaped(link.code);
    if (link.title.isNotEmpty())
        text << "&title=" << escaped(link.title);
    return text;
}

juce::String toText(const HostLink& link)
{
    return "micinstation://server?server=" + escaped(addressText(link.server))
           + "&key=" + link.server.fingerprint + "&host=" + escaped(link.hostKey);
}

std::optional<JoinLink> parseJoinLink(const juce::String& text)
{
    const auto parameters = linkParameters(text, "join");
    if (!parameters)
        return std::nullopt;
    const auto server = serverFrom(parameter(*parameters, "server"), parameter(*parameters, "key"));
    const auto code = parameter(*parameters, "code");
    if (!server || code.isEmpty())
        return std::nullopt;
    return JoinLink{*server, code, parameter(*parameters, "title").substring(0, 100)};
}

std::optional<HostLink> parseHostLink(const juce::String& text)
{
    const auto parameters = linkParameters(text, "server");
    if (!parameters)
        return std::nullopt;
    const auto server = serverFrom(parameter(*parameters, "server"), parameter(*parameters, "key"));
    const auto key = parameter(*parameters, "host");
    if (!server || key.isEmpty())
        return std::nullopt;
    return HostLink{*server, key};
}

} // namespace daw::coop
