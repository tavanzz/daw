#include "daw/services/AppLog.h"

#include "daw/services/UserSettings.h"

#include <algorithm>

namespace daw::services
{

namespace
{

constexpr const char* fileExtension = ".log";

juce::Array<juce::File> sessionFiles(const juce::File& folder, const juce::String& applicationName)
{
    auto files = folder.findChildFiles(juce::File::findFiles, false, applicationName + " *" + fileExtension);
    // Names carry the start time, so their order is the sessions' order.
    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    return files;
}

juce::String lastLineOf(const juce::File& file)
{
    auto lines = juce::StringArray::fromLines(file.loadFileAsString());
    lines.removeEmptyStrings();
    return lines.isEmpty() ? juce::String() : lines[lines.size() - 1];
}

} // namespace

juce::File AppLog::defaultFolder(const juce::String& applicationName)
{
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Logs")
        .getChildFile(applicationName);
#else
    return UserSettings::defaultFolder(applicationName).getChildFile("Logs");
#endif
}

AppLog::AppLog(Options logOptions)
    : options(std::move(logOptions))
{
    options.folder.createDirectory();

    const auto previous = sessionFiles(options.folder, options.applicationName);
    if (!previous.isEmpty())
        previousEndedNormally = lastLineOf(previous.getLast()).endsWith(endOfSessionLine);

    // The start time names the file, to the millisecond: names sort as sessions do.
    const auto now = juce::Time::getCurrentTime();
    const auto name = options.applicationName + " " + now.formatted("%Y-%m-%d %H-%M-%S") + "." +
                      juce::String(now.getMilliseconds()).paddedLeft('0', 3);
    file = options.folder.getNonexistentChildFile(name, fileExtension, false);
    stream = std::make_unique<juce::FileOutputStream>(file);
    if (stream->failedToOpen())
        stream.reset();

    removeOldSessions();
}

AppLog::~AppLog()
{
    if (juce::Logger::getCurrentLogger() == this)
        juce::Logger::setCurrentLogger(nullptr);
}

void AppLog::removeOldSessions()
{
    auto files = sessionFiles(options.folder, options.applicationName);
    for (int index = 0; index < files.size() - std::max(options.sessionsKept, 1); ++index)
        if (files[index] != file)
            files[index].deleteFile();
}

juce::String AppLog::maskHome(const juce::String& text, const juce::File& home)
{
    const auto path = home.getFullPathName();
    if (path.length() < 2)
        return text;

    // Paths compare without case where the file system does (macOS, Windows).
    juce::String masked;
    int start = 0;
    for (;;)
    {
        const auto found = juce::File::areFileNamesCaseSensitive() ? text.indexOf(start, path)
                                                                   : text.indexOfIgnoreCase(start, path);
        if (found < 0)
            break;
        masked << text.substring(start, found) << "~";
        start = found + path.length();
    }
    return masked + text.substring(start);
}

void AppLog::logMessage(const juce::String& message)
{
    const auto now = juce::Time::getCurrentTime();
    const auto line = now.formatted("%Y-%m-%d %H:%M:%S") + "." +
                      juce::String(now.getMilliseconds()).paddedLeft('0', 3) + "  " +
                      maskHome(message, options.home) + "\n";

    const juce::ScopedLock sl(lock);
    // A full log still gets its last line, which tells the next session this one ended well.
    if (stream == nullptr || (full && message != endOfSessionLine))
        return;

    const auto bytes = static_cast<juce::int64>(line.getNumBytesAsUTF8());
    if (!full && bytesWritten + bytes > options.maxBytesPerSession && message != endOfSessionLine)
    {
        full = true;
        stream->writeText("The log is full: later messages of this session are left out.\n", false,
                          false, "\n");
    }
    else
    {
        stream->writeText(line, false, false, "\n");
        bytesWritten += bytes;
    }
    // Written at once, so that a crash leaves everything before it in the file.
    stream->flush();
}

} // namespace daw::services
