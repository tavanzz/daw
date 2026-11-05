#include "daw/engine/rt/ForeignCode.h"

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#define DAW_HAS_RTSAN 1
#endif
#endif

namespace daw::engine::rt
{

namespace
{
// Constant-initialised: reading it from inside operator new never allocates.
constinit thread_local int depth = 0;
} // namespace

ForeignCode::ForeignCode() noexcept
{
    ++depth;
#if defined(DAW_HAS_RTSAN)
    __rtsan_disable();
#endif
}

ForeignCode::~ForeignCode()
{
#if defined(DAW_HAS_RTSAN)
    __rtsan_enable();
#endif
    --depth;
}

bool ForeignCode::isRunning() noexcept
{
    return depth > 0;
}

} // namespace daw::engine::rt
