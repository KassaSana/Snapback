// AppKit backend for reveal_path.hpp. Roadmap 7.6.
#if defined(__APPLE__)

#include "app/reveal_path.hpp"

#import <AppKit/AppKit.h>

namespace snapback {
namespace detail {

bool reveal_existing_directory(const std::filesystem::path& dir) {
    // Called from a webview IPC handler, which the webview delivers on the main thread —
    // AppKit's requirement. Guarded rather than assumed, the same way mac_ui.mm does it, so a
    // future off-thread caller fails a check instead of corrupting AppKit state.
    if (![NSThread isMainThread]) return false;

    // The webview thread has no autorelease pool of its own, so the NSString/NSURL below
    // would leak without one.
    @autoreleasepool {
        // stringWithUTF8String: returns nil on invalid UTF-8, which a filesystem path on macOS
        // should never be — but nil would otherwise reach fileURLWithPath: and raise.
        NSString* path = [NSString stringWithUTF8String:dir.c_str()];
        if (path == nil) return false;

        NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
        if (url == nil) return false;

        // Launch Services activates Finder on this URL. No child process, no shell, and no
        // string parsing — the path never stops being a path.
        return [[NSWorkspace sharedWorkspace] openURL:url] == YES;
    }
}

}  // namespace detail
}  // namespace snapback

#endif  // __APPLE__
