#include "snapback/focus_window.hpp"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#include <string>

namespace snapback {

bool focus_window_supported() {
    return true;
}

namespace detail {

FocusTargetResult focus_window_native(const std::string& app_name,
                                      const std::string& /*window_title*/) {
    @autoreleasepool {
        NSString* targetName = [NSString stringWithUTF8String:app_name.c_str()];
        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        NSArray<NSRunningApplication*>* apps = [workspace runningApplications];

        NSRunningApplication* matched = nil;
        for (NSRunningApplication* app in apps) {
            if ([app.localizedName caseInsensitiveCompare:targetName] == NSOrderedSame ||
                [app.bundleIdentifier caseInsensitiveCompare:targetName] == NSOrderedSame) {
                matched = app;
                break;
            }
        }

        if (!matched) {
            return FocusTargetResult{
                false, "Could not find a running application for '" + app_name + "'"};
        }

        const BOOL activated = [matched activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        if (activated) {
            return FocusTargetResult{true, "Application activated successfully"};
        }

        return FocusTargetResult{false, "Failed to bring application to foreground"};
    }
}

}  // namespace detail
}  // namespace snapback

#endif  // __APPLE__
