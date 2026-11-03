#include "support/WindowManagerAtoms.h"

// Xlib's macros (None, Bool, True, ...) stay inside this file.
#ifdef __linux__
 #include <X11/Xlib.h>
#endif

namespace daw::test
{

WindowManagerAtoms::WindowManagerAtoms()
{
#ifdef __linux__
    auto* connection = XOpenDisplay(nullptr);
    display = connection;

    if (connection == nullptr)
        return;

    // The atoms JUCE looks up without creating (juce_XWindowSystem_linux.cpp).
    for (const char* name :
         {"WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "WM_CHANGE_STATE", "WM_STATE",
          "KWM_WIN_DECORATION", "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", "_MOTIF_WM_HINTS",
          "_NET_FRAME_EXTENTS", "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_MOVERESIZE", "_NET_WM_PING",
          "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_HIDDEN",
          "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_WINDOW_TYPE",
          "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WORKAREA",
          "_WIN_HINTS"})
        XInternAtom(connection, name, False);

    XSync(connection, False);
#endif
}

WindowManagerAtoms::~WindowManagerAtoms()
{
#ifdef __linux__
    if (display != nullptr)
        XCloseDisplay(static_cast<Display*>(display));
#endif
}

} // namespace daw::test
