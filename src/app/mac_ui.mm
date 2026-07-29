// AppKit side of mac_ui.hpp. Roadmap 3.1.
#if defined(__APPLE__)

#include "app/mac_ui.hpp"

#import <Cocoa/Cocoa.h>

namespace snapback {
namespace mac {

void bring_window_to_front(void* ns_window) {
    // Called from tray menu callbacks, which Cocoa already delivers on the main thread.
    // Guarded anyway so a future off-thread caller degrades to doing nothing instead of
    // corrupting AppKit state.
    if (![NSThread isMainThread]) return;

    // deminiaturize first: makeKeyAndOrderFront on a minimised window is a no-op, so
    // without this the tray item would appear to do nothing for a window in the Dock —
    // the exact case the menu item exists to fix.
    if (auto* window = static_cast<NSWindow*>(ns_window)) {
        if (window.isMiniaturized) [window deminiaturize:nil];
        [window makeKeyAndOrderFront:nil];
    }

    // ignoringOtherApps:YES because the user asked for this window by clicking the menu
    // bar; a polite activation would queue behind whatever is frontmost and leave the
    // window hidden.
    [NSApp activateIgnoringOtherApps:YES];
}

}  // namespace mac
}  // namespace snapback

#endif  // __APPLE__
