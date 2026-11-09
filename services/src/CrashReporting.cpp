#include "daw/services/CrashReporting.h"

#include "daw/services/AppLog.h"
#include "daw/services/BuildInfo.h"
#include "daw/services/Http.h"

#include <sentry.h>

#include <algorithm>
#include <string>

namespace daw::services
{

namespace
{

constexpr int reportsKept = 20;
constexpr int outboxKept = 20;
constexpr juce::int64 outboxMaxAgeMilliseconds = 30LL * 24 * 60 * 60 * 1000;

// sentry-native needs a DSN to capture anything; builds without one never send.
constexpr const char* localOnlyDsn = "https://00000000000000000000000000000000@localhost/0";

juce::var maskHomeEverywhere(const juce::var& value, const juce::File& home)
{
    if (value.isString())
        return AppLog::maskHome(value.toString(), home);
    if (const auto* array = value.getArray())
    {
        juce::Array<juce::var> masked;
        for (const auto& element : *array)
            masked.add(maskHomeEverywhere(element, home));
        return masked;
    }
    if (const auto* object = value.getDynamicObject())
    {
        auto masked = std::make_unique<juce::DynamicObject>();
        for (const auto& property : object->getProperties())
            masked->setProperty(property.name, maskHomeEverywhere(property.value, home));
        return juce::var(masked.release());
    }
    return value;
}

// Only the listed properties of an object, if any.
juce::var keepOnly(const juce::var& value, std::initializer_list<const char*> names)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return {};
    auto kept = std::make_unique<juce::DynamicObject>();
    for (const auto* name : names)
        if (object->hasProperty(name))
            kept->setProperty(name, object->getProperty(name));
    return juce::var(kept.release());
}

struct EnvelopeItem
{
    juce::var header;
    std::string payload;
};

// A Sentry envelope: a header line, then items (a header line, then the payload, of the
// header's "length" in bytes or up to the end of the line).
bool parseEnvelope(const std::string& text, juce::var& header, std::vector<EnvelopeItem>& items)
{
    auto position = text.find('\n');
    if (position == std::string::npos)
        return false;
    header = juce::JSON::parse(juce::String::fromUTF8(text.data(), static_cast<int>(position)));
    ++position;

    while (position < text.size())
    {
        const auto end = text.find('\n', position);
        if (end == std::string::npos)
            break;
        EnvelopeItem item;
        item.header = juce::JSON::parse(juce::String::fromUTF8(text.data() + position, static_cast<int>(end - position)));
        position = end + 1;
        if (!item.header.isObject())
            return false;

        if (item.header.hasProperty("length"))
        {
            const auto length = static_cast<size_t>(static_cast<juce::int64>(item.header["length"]));
            if (position + length > text.size())
                return false;
            item.payload = text.substr(position, length);
            position += length;
            if (position < text.size() && text[position] == '\n')
                ++position;
        }
        else
        {
            // In parentheses: sentry.h brings windows.h, whose min macro would take it.
            const auto payloadEnd = (std::min)(text.find('\n', position), text.size());
            item.payload = text.substr(position, payloadEnd - position);
            position = payloadEnd + 1;
        }
        items.push_back(std::move(item));
    }
    return header.isObject();
}

struct Dsn
{
    juce::String endpoint, key;
};

// https://<key>@<host>/<project> -> the envelope endpoint and the public key.
Dsn parseDsn(const juce::String& dsn)
{
    const auto withoutScheme = dsn.fromFirstOccurrenceOf("://", false, false);
    const auto key = withoutScheme.upToFirstOccurrenceOf("@", false, false);
    const auto hostAndPath = withoutScheme.fromFirstOccurrenceOf("@", false, false);
    const auto host = hostAndPath.upToFirstOccurrenceOf("/", false, false);
    const auto project = hostAndPath.fromLastOccurrenceOf("/", false, false);
    if (key.isEmpty() || host.isEmpty() || project.isEmpty() || !dsn.startsWith("https://"))
        return {};
    return {"https://" + host + "/api/" + project + "/envelope/", key};
}

juce::Array<juce::File> filesOldestFirst(const juce::File& folder, const juce::String& pattern)
{
    auto files = folder.findChildFiles(juce::File::findFiles, false, pattern);
    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    return files;
}

void keepNewest(const juce::File& folder, const juce::String& pattern, int count)
{
    const auto files = filesOldestFirst(folder, pattern);
    for (int index = 0; index < files.size() - count; ++index)
        files[index].deleteFile();
}

void sendToReporting(sentry_envelope_t* envelope, void* state)
{
    size_t size = 0;
    char* text = sentry_envelope_serialize(envelope, &size);
    const auto serialized = text != nullptr ? juce::String::fromUTF8(text, static_cast<int>(size)) : juce::String();
    sentry_free(text);
    sentry_envelope_free(envelope);
    if (serialized.isNotEmpty())
        static_cast<CrashReporting*>(state)->receiveEnvelope(serialized);
}

} // namespace

// Sends the outbox on a thread of its own, when woken.
class CrashReporting::Sender final : public juce::Thread
{
public:
    explicit Sender(CrashReporting& owner)
        : juce::Thread("Crash reports")
        , reporting(owner)
    {
    }

    ~Sender() override { stopThread(10000); }

    void run() override
    {
        while (!threadShouldExit())
        {
            reporting.sendPending();
            wait(-1);
        }
    }

private:
    CrashReporting& reporting;
};

CrashReporting::CrashReporting(Options reportingOptions)
    : options(std::move(reportingOptions))
    , sendAllowed(options.sendAllowed)
{
    options.databaseFolder.createDirectory();
    options.reportsFolder.createDirectory();
    getOutbox().createDirectory();

    auto* sentryOptions = sentry_options_new();
    sentry_options_set_dsn(sentryOptions, (options.dsn.isNotEmpty() ? options.dsn : juce::String(localOnlyDsn)).toRawUTF8());
#if JUCE_WINDOWS
    sentry_options_set_database_pathw(sentryOptions, options.databaseFolder.getFullPathName().toWideCharPointer());
#else
    sentry_options_set_database_path(sentryOptions, options.databaseFolder.getFullPathName().toRawUTF8());
#endif
    sentry_options_set_release(sentryOptions, options.release.toRawUTF8());
    sentry_options_set_environment(sentryOptions, options.environment.toRawUTF8());
    // Nothing about the user (sentry-native adds none unless told to), no sessions, no
    // breadcrumbs; and whatever an event holds is scrubbed again in receiveEnvelope().
    sentry_options_set_auto_session_tracking(sentryOptions, 0);
    sentry_options_set_max_breadcrumbs(sentryOptions, 0);
    sentry_options_set_debug(sentryOptions, 0);
    sentry_options_set_symbolize_stacktraces(sentryOptions, 0);

    auto* transport = sentry_transport_new(&sendToReporting);
    sentry_transport_set_state(transport, this);
    sentry_options_set_transport(sentryOptions, transport);

    // Reports of the last crash come through the transport during this call.
    started = sentry_init(sentryOptions) == 0;

    if (options.sendInBackground)
    {
        sender = std::make_unique<Sender>(*this);
        sender->startThread();
    }
}

CrashReporting::~CrashReporting()
{
    sender.reset();
    if (started)
        sentry_close();
}

juce::File CrashReporting::getOutbox() const
{
    return options.databaseFolder.getChildFile("outbox");
}

void CrashReporting::setSendAllowed(bool allowed)
{
    sendAllowed = allowed;
    if (!allowed)
        for (const auto& file : filesOldestFirst(getOutbox(), "*.envelope"))
            file.deleteFile();
    else if (sender != nullptr)
        sender->notify();
}

juce::Array<juce::File> CrashReporting::getLocalReports() const
{
    auto files = filesOldestFirst(options.reportsFolder, "Crash *.json");
    std::reverse(files.begin(), files.end());
    return files;
}

juce::var CrashReporting::scrubEvent(const juce::var& event, const juce::File& home, const juce::StringArray& crashedPlugins)
{
    if (!event.isObject())
        return {};

    // What a crash needs to be understood, nothing else.
    auto kept = keepOnly(event, {"event_id", "timestamp", "platform", "level", "release", "environment", "exception",
                                 "threads", "debug_meta", "sdk", "dist"});
    auto* object = kept.getDynamicObject();

    auto contexts = std::make_unique<juce::DynamicObject>();
    if (const auto os = keepOnly(event["contexts"]["os"], {"name", "version", "build", "type"}); os.isObject())
        contexts->setProperty("os", os);
    // The processor's architecture, which symbolication needs (the build's own slice).
    auto device = keepOnly(event["contexts"]["device"], {"arch"});
    if (!device.isObject())
        device = juce::var(new juce::DynamicObject());
    if (!device.hasProperty("arch"))
        device.getDynamicObject()->setProperty("arch", buildInfo().architecture);
    contexts->setProperty("device", device);
    object->setProperty("contexts", juce::var(contexts.release()));

    auto tags = std::make_unique<juce::DynamicObject>();
    if (!crashedPlugins.isEmpty())
        tags->setProperty("plugin", crashedPlugins.joinIntoString(", "));
    object->setProperty("tags", juce::var(tags.release()));

    return maskHomeEverywhere(kept, home);
}

juce::String CrashReporting::scrubEnvelope(const juce::String& envelope, const juce::File& home,
                                           const juce::StringArray& crashedPlugins)
{
    juce::var header;
    std::vector<EnvelopeItem> items;
    if (!parseEnvelope(envelope.toStdString(), header, items))
        return {};

    for (const auto& item : items)
    {
        if (item.header["type"].toString() != "event")
            continue; // sessions, attachments and the rest stay home

        const auto event = scrubEvent(juce::JSON::parse(juce::String::fromUTF8(item.payload.data(),
                                                                              static_cast<int>(item.payload.size()))),
                                      home, crashedPlugins);
        if (!event.isObject())
            return {};

        const auto payload = juce::JSON::toString(event, true);
        auto envelopeHeader = keepOnly(header, {"event_id", "sent_at", "sdk"});
        auto itemHeader = std::make_unique<juce::DynamicObject>();
        itemHeader->setProperty("type", "event");
        itemHeader->setProperty("length", static_cast<juce::int64>(payload.getNumBytesAsUTF8()));
        return juce::JSON::toString(envelopeHeader, true) + "\n" + juce::JSON::toString(juce::var(itemHeader.release()), true) +
               "\n" + payload + "\n";
    }
    return {};
}

void CrashReporting::receiveEnvelope(const juce::String& envelope)
{
    const auto scrubbed = scrubEnvelope(envelope, options.home, options.crashedPlugins);
    if (scrubbed.isEmpty())
        return;

    // The readable copy that stays on this computer.
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
    const auto eventText = scrubbed.fromFirstOccurrenceOf("\n", false, false).fromFirstOccurrenceOf("\n", false, false);
    options.reportsFolder.getNonexistentChildFile("Crash " + stamp, ".json", false).replaceWithText(eventText, false, false, "\n");
    keepNewest(options.reportsFolder, "Crash *.json", reportsKept);
    juce::Logger::writeToLog("A crash report of the last session was kept" +
                             juce::String(sendAllowed.load() && canSend() ? " and will be sent" : ""));

    if (!sendAllowed.load() || !canSend())
        return;
    getOutbox().getNonexistentChildFile(stamp, ".envelope", false).replaceWithText(scrubbed, false, false, "\n");
    keepNewest(getOutbox(), "*.envelope", outboxKept);
    if (sender != nullptr)
        sender->notify();
}

void CrashReporting::sendPending()
{
    if (!sendAllowed.load() || !canSend())
        return;
    const auto dsn = parseDsn(options.dsn);
    if (dsn.endpoint.isEmpty())
        return;

    const auto now = juce::Time::getCurrentTime();
    for (const auto& file : filesOldestFirst(getOutbox(), "*.envelope"))
    {
        if ((now - file.getLastModificationTime()).inMilliseconds() > outboxMaxAgeMilliseconds)
        {
            file.deleteFile();
            continue;
        }
        juce::MemoryBlock body;
        if (!file.loadFileAsData(body))
            continue;
        const auto answer = http::post(juce::URL(dsn.endpoint), body,
                                       "Content-Type: application/x-sentry-envelope\r\nX-Sentry-Auth: Sentry "
                                       "sentry_version=7, sentry_key=" +
                                           dsn.key + ", sentry_client=" + http::userAgent());
        if (!answer.ok)
        {
            juce::Logger::writeToLog("A crash report could not be sent yet: " + answer.error);
            return; // no network: the next start tries again
        }
        file.deleteFile();
        juce::Logger::writeToLog("A crash report was sent");
    }
}

} // namespace daw::services
