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
        // Returns nil for input that is not valid UTF-8. Comparing against nil below would be
        // undefined, and this interface promises never to throw.
        if (!targetName) {
            return FocusTargetResult{false, "Could not find a running application: the name is not valid text"};
        }

        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        NSArray<NSRunningApplication*>* apps = [workspace runningApplications];

        // Both properties are nullable -- `bundleIdentifier` is nil for a great many running
        // processes -- and messaging nil in Objective-C returns 0, which is the value of
        // NSOrderedSame. Comparing without the nil check therefore reports a *match* on every
        // such app, so a name matching nothing would activate whichever one came first and
        // report success. That is what made this read as "focus_window cannot detect failure"
        // when the real fault was matching far too eagerly.
        NSRunningApplication* matched = nil;
        for (NSRunningApplication* app in apps) {
            NSString* localizedName = app.localizedName;
            NSString* bundleIdentifier = app.bundleIdentifier;
            const BOOL nameMatches =
                localizedName != nil &&
                [localizedName caseInsensitiveCompare:targetName] == NSOrderedSame;
            const BOOL bundleMatches =
                bundleIdentifier != nil &&
                [bundleIdentifier caseInsensitiveCompare:targetName] == NSOrderedSame;
            if (nameMatches || bundleMatches) {
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
