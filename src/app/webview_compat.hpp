// The one include site for webview/webview.h — include this, never the raw header.
//
// Why: on Linux, webview.h pulls GTK → GDK → X11, and X11's headers #define plain
// words as macros: KeyPress, KeyRelease, None, Status, Bool, True, False, Success,
// Always. Those clobber our identifiers at parse time — `EventType::KeyPress`
// becomes `EventType::2`, `using Status = ...` becomes `using int = ...` — which is
// exactly how the first-ever run of the desktop-app-build guard failed on Ubuntu
// (the job was `needs:`-skipped until Roadmap 6.3, so this never surfaced before).
//
// The scrub below undefs the pollution right after the include. Our code never
// calls X11 directly, so nothing here needs the macros; any future TU that does
// raw X11 work must include <X11/Xlib.h> itself, after our headers.
//
// macOS/Windows never define these, and #undef on an undefined name is a no-op,
// so the scrub is unconditional.
//
// The same problem arrives from the other direction on Windows. webview reaches WebView2,
// which reaches windows.h, which defines `min` and `max` as function-like macros unless
// NOMINMAX is set. Every `std::max(a, b)` compiled afterwards then expands to
// `std::((a) > (b) ? (a) : (b))` — MSVC C2589, "illegal token on right side of '::'". That is
// what broke the Windows app build at focus_summary.hpp and logger.hpp, and it is the same
// cause command_dispatch.hpp already works around by hand with `(std::min)`.
//
// Only MSVC ever sees it: libstdc++ #undefs both macros itself, so GCC — including the
// windows-gcc CI job — compiles this cleanly no matter what windows.h did. Defining NOMINMAX
// here fixes it once, at the single include site, rather than once per call site forever.
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
