#pragma once

namespace daw::test
{

/**
    On Linux, creates the X11 atoms a window manager would create (WM_PROTOCOLS,
    _NET_WM_WINDOW_TYPE, ...) and keeps a connection to the display while it lives.

    CI runs the UI tests on a bare virtual display: with no window manager those atoms
    do not exist, JUCE (which only looks them up) uses atom 0 when it opens a window,
    and the X server ends the process. The connection stays open because an X server
    with no client left resets, which would delete them again. Elsewhere it does
    nothing.
*/
class WindowManagerAtoms final
{
public:
    WindowManagerAtoms();
    ~WindowManagerAtoms();

    WindowManagerAtoms(const WindowManagerAtoms&) = delete;
    WindowManagerAtoms& operator=(const WindowManagerAtoms&) = delete;

private:
    [[maybe_unused]] void* display = nullptr; // Linux only
};

} // namespace daw::test
