#include "app/window_lifecycle.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include <functional>
#include <utility>

// See the header: one main window per process, so the hook is a file-local rather than a
// context pointer threaded through the delegate.
static std::function<void()> g_on_hidden;

@interface SnapbackCloseToTrayDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, weak) id<NSWindowDelegate> originalDelegate;
@end

@implementation SnapbackCloseToTrayDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [sender orderOut:nil];
    // After the hide, not before: the hook may put a notification on screen, and it should
    // arrive next to a status item the user can already see rather than over the window that is
    // about to vanish. windowShouldClose: runs on the main thread, which is what it requires.
    if (g_on_hidden) g_on_hidden();
    return NO;
}

- (BOOL)respondsToSelector:(SEL)aSelector {
    if (aSelector == @selector(windowShouldClose:)) {
        return YES;
    }
    return [self.originalDelegate respondsToSelector:aSelector];
}

- (id)forwardingTargetForSelector:(SEL)aSelector {
    return self.originalDelegate;
}

@end

namespace snapback {

static const char* kDelegateKey = "SnapbackCloseToTrayDelegateKey";

void enable_close_to_tray(void* native_window, std::function<void()> on_hidden) {
    g_on_hidden = std::move(on_hidden);
    enable_close_to_tray(native_window);
}

void enable_close_to_tray(void* native_window) {
    if (!native_window) return;
    if (![NSThread isMainThread]) return;

    NSWindow* window = (__bridge NSWindow*)native_window;
    SnapbackCloseToTrayDelegate* delegate = [[SnapbackCloseToTrayDelegate alloc] init];
    delegate.originalDelegate = window.delegate;
    objc_setAssociatedObject(window, kDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    window.delegate = delegate;
}

void prepare_app_exit(void* native_window) {
    // Cleared first, and unconditionally: an explicit Quit is not a close-to-tray hide, so
    // nothing should tell the user the app is still running while it is on its way out.
    g_on_hidden = nullptr;
    if (!native_window) return;
    if (![NSThread isMainThread]) return;

    NSWindow* window = (__bridge NSWindow*)native_window;
    SnapbackCloseToTrayDelegate* delegate = objc_getAssociatedObject(window, kDelegateKey);
    if (delegate) {
        window.delegate = delegate.originalDelegate;
        objc_setAssociatedObject(window, kDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

bool is_close_to_tray_enabled(void* native_window) {
    if (!native_window) return false;
    NSWindow* window = (__bridge NSWindow*)native_window;
    return objc_getAssociatedObject(window, kDelegateKey) != nil;
}

}  // namespace snapback
#endif
