#pragma once

#include <functional>

namespace daw::engine::io
{

/**
    The operating system's permission to record from the microphone and audio
    inputs (macOS and iOS ask the user once; elsewhere it is always granted).

    The app asks only when an input is first needed, never at launch, and opens the
    device's inputs only once it is granted.
*/
class MicrophoneAccess
{
public:
    enum class Status
    {
        notDetermined, ///< the user has not been asked yet
        granted,
        denied ///< refused, or not allowed on this machine
    };

    [[nodiscard]] static Status getStatus();

    /** Asks the user if they have not answered yet, then calls back on the message
        thread with the answer (at once if it is known). */
    static void request(std::function<void(bool granted)> callback);
};

} // namespace daw::engine::io
