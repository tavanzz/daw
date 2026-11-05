#include "CrashMarker.h"

#include "daw/engine/plugins/RunningPlugin.h"

#include <array>
#include <csignal>
#include <cstring>

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace daw::app
{

namespace
{

// Where to write, ready before any crash: a handler may not allocate.
std::array<char, 2048> markerPath{};

#if JUCE_WINDOWS
int openMarker() noexcept
{
    int handle = -1;
    _sopen_s(&handle, markerPath.data(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _SH_DENYNO,
             _S_IREAD | _S_IWRITE);
    return handle;
}

void writeBytes(int handle, const char* bytes, std::size_t size) noexcept
{
    (void) _write(handle, bytes, static_cast<unsigned int>(size));
}

void closeMarker(int handle) noexcept
{
    (void) _close(handle);
}
#else
int openMarker() noexcept
{
    return ::open(markerPath.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

void writeBytes(int handle, const char* bytes, std::size_t size) noexcept
{
    (void) !::write(handle, bytes, size);
}

void closeMarker(int handle) noexcept
{
    (void) ::close(handle);
}
#endif

void writeLine(int handle, const char* name) noexcept
{
    if (name == nullptr)
        return;
    writeBytes(handle, name, std::strlen(name));
    writeBytes(handle, "\n", 1);
}

void writeMarker() noexcept
{
    const char* audio = engine::plugins::RunningPlugin::onAudioThread();
    const char* other = engine::plugins::RunningPlugin::elsewhere();
    if (audio == nullptr && other == nullptr)
        return;
    const int handle = openMarker();
    if (handle < 0)
        return;
    writeLine(handle, audio);
    writeLine(handle, other);
    closeMarker(handle);
}

#if !JUCE_WINDOWS
extern "C" void onCrashSignal(int signalNumber)
{
    writeMarker();
    // The default action follows: the process ends as it would have, with its report.
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}
#endif

} // namespace

CrashMarker::CrashMarker(const juce::File& markerFile)
    : file(markerFile)
{
    const auto path = file.getFullPathName().toStdString();
    if (path.size() < markerPath.size())
        std::memcpy(markerPath.data(), path.c_str(), path.size() + 1);

#if JUCE_WINDOWS
    juce::SystemStats::setApplicationCrashHandler([](void*) { writeMarker(); });
#else
    for (const int signalNumber : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
        std::signal(signalNumber, onCrashSignal);
#endif
}

juce::StringArray CrashMarker::takeCrashedPlugins()
{
    if (!file.existsAsFile())
        return {};
    auto names = juce::StringArray::fromLines(file.loadFileAsString());
    names.removeEmptyStrings();
    names.removeDuplicates(false);
    file.deleteFile();
    return names;
}

} // namespace daw::app
