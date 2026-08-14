#include "app/window_lifecycle.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

@interface SnapbackCloseToTrayDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, weak) id<NSWindowDelegate> originalDelegate;
@end

@implementation SnapbackCloseToTrayDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [sender orderOut:nil];
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
