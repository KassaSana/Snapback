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
#pragma once

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
