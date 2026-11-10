// micinstation-server: the co-op server of MICINSTATION (docs/SERVER.md).

#include "daw/coop/Links.h"
#include "daw/coop/Messages.h"
#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"
#include "daw/net/Tls.h"
#include "daw/net/TlsLink.h"
#include "daw/server/Server.h"

#include <juce_core/juce_core.h>

#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#include <atomic>
#include <csignal>
#include <cstdio>

namespace
{

using namespace daw;

std::atomic<bool> stopRequested{false};

extern "C" void requestStop(int)
{
    stopRequested.store(true);
}

void print(const juce::String& line)
{
    std::fputs((line + "\n").toRawUTF8(), stdout);
    std::fflush(stdout);
}

void log(const juce::String& line)
{
    print(juce::Time::getCurrentTime().toISO8601(true) + "  " + line);
}

int fail(const juce::String& message)
{
    std::fputs(("micinstation-server: " + message + "\n").toRawUTF8(), stderr);
    return 1;
}

const char* usage = R"(MICINSTATION co-op server

  micinstation-server [options]           runs the server
  micinstation-server --links [options]   prints the host link of every host key
  micinstation-server --new-host-key [options]
                                          adds a host key and prints its link
  micinstation-server --revoke-host-key <start of the key> [options]
  micinstation-server --probe <link>      connects to a server as the app does
  micinstation-server --version

Options (or the environment variables in brackets):
  --data <folder>          where sessions, files, keys and the certificate are kept
                           (MICINSTATION_DATA; default ./micinstation-data)
  --port <port>            the port to listen on (MICINSTATION_PORT; default 7443)
  --bind <address>         the address to listen on (default: every one)
  --public <host[:port]>   how the app reaches this server, for links
                           (MICINSTATION_PUBLIC; default localhost and --port)
  --max-sessions <n>       default 20
  --max-participants <n>   per session, default 8
  --session-gb <n>         files a session may keep, default 20
  --total-gb <n>           files of all sessions, default 100
)";

struct Arguments
{
    juce::StringArray words;

    [[nodiscard]] bool has(const juce::String& name) const { return words.contains(name); }

    [[nodiscard]] juce::String value(const juce::String& name, const juce::String& variable,
                                     const juce::String& fallback) const
    {
        const int index = words.indexOf(name);
        if (index >= 0 && index + 1 < words.size())
            return words[index + 1];
        const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable(variable, {});
        return fromEnvironment.isNotEmpty() ? fromEnvironment : fallback;
    }
};

juce::File hostKeysFile(const juce::File& data)
{
    return data.getChildFile("host-keys.txt");
}

juce::StringArray readHostKeys(const juce::File& data)
{
    juce::StringArray keys;
    juce::StringArray lines;
    lines.addLines(hostKeysFile(data).loadFileAsString());
    for (const auto& line : lines)
        if (const auto key = line.trim(); key.isNotEmpty() && !key.startsWithChar('#'))
            keys.add(key);
    return keys;
}

bool writeHostKeys(const juce::File& data, const juce::StringArray& keys)
{
    juce::String text = "# Host keys of this MICINSTATION server: whoever has one may open sessions.\n"
                        "# One per line; remove a line to revoke it (the server reads this at start).\n";
    for (const auto& key : keys)
        text << key << "\n";
    const auto file = hostKeysFile(data);
    if (!file.replaceWithText(text, false, false, "\n"))
        return false;
#if !JUCE_WINDOWS
    chmod(file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR); // keys are secrets
#endif
    return true;
}

juce::String newHostKey()
{
    std::uint8_t bytes[24];
    if (!net::secureRandom(bytes, sizeof(bytes)))
        return {};
    return net::toBase64Url(bytes, sizeof(bytes));
}

coop::ServerAddress publicAddress(const Arguments& arguments, const net::ServerIdentity& identity,
                                  int port)
{
    const auto text = arguments.value("--public", "MICINSTATION_PUBLIC", "localhost");
    coop::ServerAddress address;
    const auto colon = text.lastIndexOfChar(':');
    const bool hasPort = colon > 0 && !text.endsWithChar(']') && text.substring(colon + 1).containsOnly("0123456789");
    address.host = (hasPort ? text.substring(0, colon) : text).trimCharactersAtStart("[").trimCharactersAtEnd("]");
    address.port = hasPort ? text.substring(colon + 1).getIntValue() : port;
    address.fingerprint = identity.getFingerprint();
    return address;
}

int printLinks(const coop::ServerAddress& address, const juce::StringArray& keys)
{
    if (keys.isEmpty())
        return fail("no host keys: add one with --new-host-key");
    print("Host links (share each only with someone who may open sessions on this server):");
    for (const auto& key : keys)
        print("  " + coop::toText(coop::HostLink{address, key}));
    return 0;
}

int probe(const juce::String& text)
{
    // Connects as the app does and says hello with a host key that is surely wrong: an
    // answer (a refusal) shows that TLS, the fingerprint and the protocol all work.
    std::optional<coop::ServerAddress> server;
    if (const auto join = coop::parseJoinLink(text))
        server = join->server;
    else if (const auto host = coop::parseHostLink(text))
        server = host->server;
    if (!server)
        return fail("not a MICINSTATION link: " + text);

    net::TlsClientLink link(server->host, server->port, server->fingerprint);
    coop::Hello hello;
    hello.protocolVersion = static_cast<std::uint32_t>(model::networkProtocolVersion);
    hello.formatVersion = static_cast<std::uint32_t>(model::currentFormatVersion);
    hello.appVersion = "probe";
    hello.request = coop::CreateSession{"probe", "probe"};
    link.send(coop::encodeFrame(hello));

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        for (const auto& frame : link.receive())
            if (const auto refused = coop::decodeRefused(frame))
            {
                print("The server answered (protocol " + juce::String(refused->serverProtocol)
                      + ", project format " + juce::String(refused->serverFormat) + "): "
                      + coop::describe(*refused));
                return refused->serverProtocol == static_cast<std::uint32_t>(model::networkProtocolVersion)
                           ? 0
                           : 1;
            }
        if (link.getState() == net::Link::State::closed)
            return fail(link.getProblem().isNotEmpty() ? link.getProblem() : juce::String("the connection closed"));
        juce::Thread::sleep(20);
    }
    return fail("no answer from " + coop::addressText(*server));
}

} // namespace

int main(int argc, char* argv[])
{
    Arguments arguments;
    for (int index = 1; index < argc; ++index)
        arguments.words.add(juce::String::fromUTF8(argv[index]));

    if (arguments.has("--help") || arguments.has("-h"))
    {
        print(usage);
        return 0;
    }
    if (arguments.has("--version"))
    {
        print("micinstation-server, network protocol " + juce::String(model::networkProtocolVersion)
              + ", project format " + juce::String(model::currentFormatVersion));
        return 0;
    }
    if (arguments.has("--probe"))
        return probe(arguments.value("--probe", {}, {}));

    const juce::File data = juce::File::getCurrentWorkingDirectory().getChildFile(
        arguments.value("--data", "MICINSTATION_DATA", "micinstation-data"));
    if (!data.createDirectory())
        return fail("cannot make the data folder " + data.getFullPathName());

    const auto identity = net::ServerIdentity::loadOrCreate(data.getChildFile("identity"));
    if (!identity)
        return fail("the server's key and certificate cannot be made or read in " + data.getFullPathName());

    const int port = arguments.value("--port", "MICINSTATION_PORT", "7443").getIntValue();
    if (port <= 0 || port > 65535)
        return fail("not a port: " + arguments.value("--port", "MICINSTATION_PORT", {}));
    const auto address = publicAddress(arguments, *identity, port);

    auto keys = readHostKeys(data);
    if (arguments.has("--links"))
        return printLinks(address, keys);
    if (arguments.has("--new-host-key"))
    {
        const auto key = newHostKey();
        keys.add(key);
        if (key.isEmpty() || !writeHostKeys(data, keys))
            return fail("cannot write " + hostKeysFile(data).getFullPathName());
        print("New host link (share it only with someone who may open sessions):");
        print("  " + coop::toText(coop::HostLink{address, key}));
        return 0;
    }
    if (arguments.has("--revoke-host-key"))
    {
        const auto start = arguments.value("--revoke-host-key", {}, {});
        juce::StringArray kept;
        for (const auto& key : keys)
            if (start.isEmpty() || !key.startsWith(start))
                kept.add(key);
        if (kept.size() == keys.size() || !writeHostKeys(data, kept))
            return fail("no host key starts with \"" + start + "\"");
        print("Revoked (restart the server for it to count): " + juce::String(keys.size() - kept.size()));
        return 0;
    }

    if (keys.isEmpty())
    {
        // The first start: one host key, shown this once (the log never shows keys again).
        keys.add(newHostKey());
        if (!writeHostKeys(data, keys))
            return fail("cannot write " + hostKeysFile(data).getFullPathName());
        print("First start: a host key was made. Share this link only with someone who may open "
              "sessions on this server (it is also in " + hostKeysFile(data).getFullPathName() + "):");
        print("  " + coop::toText(coop::HostLink{address, keys[0]}));
    }

    server::ServerOptions options;
    options.hostKeys = keys;
    options.dataFolder = data;
    options.log = [](const juce::String& line) { log(line); };
    options.maximumSessions = arguments.value("--max-sessions", {}, "20").getIntValue();
    options.maximumParticipants = arguments.value("--max-participants", {}, "8").getIntValue();
    options.maximumSessionBytes =
        static_cast<std::uint64_t>(arguments.value("--session-gb", {}, "20").getLargeIntValue()) << 30;
    options.maximumTotalBytes =
        static_cast<std::uint64_t>(arguments.value("--total-gb", {}, "100").getLargeIntValue()) << 30;
    server::Server server(options);

    net::TlsServer listener(*identity);
    if (const auto started = listener.start(port, arguments.value("--bind", {}, {})); started.failed())
        return fail(started.getErrorMessage());

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    log("listening on port " + juce::String(listener.getPort()) + " (network protocol "
        + juce::String(model::networkProtocolVersion) + "), fingerprint " + identity->getFingerprint());

    const auto wallClock = [] { return static_cast<double>(juce::Time::currentTimeMillis()) * 0.001; };
    while (!stopRequested.load())
    {
        listener.getActivity().wait(20);
        for (auto& connection : listener.takeNewConnections())
            server.addConnection(std::move(connection.link), connection.address, wallClock());
        server.update(wallClock());
    }

    log("stopping");
    listener.stop();
    server.saveAll();
    return 0;
}
