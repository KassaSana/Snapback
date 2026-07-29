// Objective-C++ window shims, declared in plain C++ so main.cpp can call them.
//
// main.cpp is a .cpp and must stay one — it is the only translation unit that ties every
// subsystem together, and compiling it as Objective-C++ would make a macOS-only language
// dialect the default for the whole app. Anything that has to speak AppKit therefore
// crosses through a void* here and is implemented in mac_ui.mm.
#pragma once

#if defined(__APPLE__)

namespace snapback {
namespace mac {

// Bring the app's main window forward and activate the process.
//
// This is what the tray's "Show Snapback" needs to be worth having: the case it exists for
// is a window that is buried behind other apps or minimised to the Dock, and ordering the
// window front without also activating the app leaves it behind whatever is frontmost.
//
// `ns_window` is the NSWindow* that webview::webview::window() returns. A null pointer
// activates the app and does nothing else, which is the right degradation if the webview
// has not created its window yet.
void bring_window_to_front(void* ns_window);

}  // namespace mac
}  // namespace snapback

#endif  // __APPLE__
