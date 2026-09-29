// The one include site for webview/webview.h -- include this, never the raw header.
//
// Linux: webview.h pulls in X11, which #defines KeyPress, None, Status, Bool, True, False,
// Success, Always, etc. and clobbers our identifiers; the scrub below undefines them. A TU that
// needs raw X11 must include <X11/Xlib.h> itself, after our headers.
//
// Windows: windows.h defines min/max macros unless NOMINMAX is set, breaking std::max under
// MSVC. Defined here once.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <webview/webview.h>  // from FetchContent

#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#undef MotionNotify
#undef FocusIn
#undef FocusOut
#undef None
#undef Status
#undef Success
#undef Always
#undef Bool
#undef True
#undef False
// Belt and braces for min/max: NOMINMAX above only helps if nothing reached windows.h before
// this header did. A future TU that includes windows.h first would still arrive here with the
// macros live, and the scrub is where that gets fixed.
#undef min
#undef max
