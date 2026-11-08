#pragma once

namespace daw::engine::plugins
{

/**
    Which plugin is running code now, for the app's crash handler: if the app dies
    inside a plugin, the next start can tell which one and offer to open the project
    with it switched off. One slot for the audio thread, one for the rest (the message
    thread loading, preparing, saving a plugin). Reading never allocates or locks, so
    a signal handler may read it.

    RAII; nests (the outer name comes back); real-time safe.
*/
class RunningPlugin
{
public:
    /** name must outlive the scope (a plugin keeps its own). */
    RunningPlugin(const char* name, bool audioThread) noexcept;
    ~RunningPlugin();

    RunningPlugin(const RunningPlugin&) = delete;
    RunningPlugin& operator=(const RunningPlugin&) = delete;

    /** The plugin running on the audio thread now; null if none. */
    [[nodiscard]] static const char* onAudioThread() noexcept;
    /** The plugin running elsewhere now; null if none. */
    [[nodiscard]] static const char* elsewhere() noexcept;

private:
    const char* previous;
    bool audio;
};

} // namespace daw::engine::plugins
