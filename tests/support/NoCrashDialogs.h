#pragma once

// On Windows, a failed assertion of the C runtime's debug library (an empty optional read,
// an iterator out of range) or an abort opens a dialog and waits for someone to click it:
// on CI nobody does, and the job hangs until its timeout. The test programs report such
// failures on stderr instead, and fail at once.

#if defined(_WIN32)
 #include <crtdbg.h>
 #include <cstdlib>
#endif

namespace daw::test
{

inline void reportCrashesWithoutDialogs() noexcept
{
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
}

} // namespace daw::test
