#include "daw/engine/plugins/RunningPlugin.h"

#include <atomic>

namespace daw::engine::plugins
{

namespace
{
std::atomic<const char*> audioThreadPlugin{nullptr};
std::atomic<const char*> otherThreadPlugin{nullptr};
static_assert(std::atomic<const char*>::is_always_lock_free);
} // namespace

RunningPlugin::RunningPlugin(const char* name, bool audioThread) noexcept
    : previous((audioThread ? audioThreadPlugin : otherThreadPlugin).exchange(name))
    , audio(audioThread)
{
}

RunningPlugin::~RunningPlugin()
{
    (audio ? audioThreadPlugin : otherThreadPlugin).store(previous);
}

const char* RunningPlugin::onAudioThread() noexcept
{
    return audioThreadPlugin.load();
}

const char* RunningPlugin::elsewhere() noexcept
{
    return otherThreadPlugin.load();
}

} // namespace daw::engine::plugins
