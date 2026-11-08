#pragma once

#include <juce_core/juce_core.h>

namespace daw::app
{

/**
    Notes, when the app crashes, which plugin was running then (engine::plugins::
    RunningPlugin, on the audio thread or elsewhere), in a file next to the user
    settings, so that the next start can keep it from loading. A crash outside any
    plugin writes nothing.

    On macOS and Linux the signal handlers write the file with system calls only and
    then let the signal take its course (the system's crash report still appears);
    on Windows JUCE's unhandled-exception filter does it.
*/
class CrashMarker
{
public:
    /** Installs the handlers; one per process. */
    explicit CrashMarker(const juce::File& markerFile);

    CrashMarker(const CrashMarker&) = delete;
    CrashMarker& operator=(const CrashMarker&) = delete;

    /** The plugins ("name (format)") the last crash named, and forgets them. */
    [[nodiscard]] juce::StringArray takeCrashedPlugins();

private:
    juce::File file;
};

} // namespace daw::app
