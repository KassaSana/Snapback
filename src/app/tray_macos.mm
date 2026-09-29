// macOS tray: an NSStatusItem with an NSMenu built from tray_menu_entries().
//
// 1. No message pump of our own: menu clicks arrive on the main run loop (which webview runs),
//    so install() refuses off the main thread.
// 2. Notifications are unimplemented: UNUserNotificationCenter needs a bundle identifier.
//    show_notification() returns false, like NoopTray.
#if defined(__APPLE__)

#include "app/tray.hpp"

#import <Cocoa/Cocoa.h>

#include <functional>
#include <utility>

// Bridges NSMenuItem clicks back into the C++ callbacks. Compiled without ARC (CMake
// passes plain -x objective-c++), so std::function ivars are constructed and destroyed
// normally and manual retain/release rules apply to the Cocoa objects below.
@interface SnapbackTrayTarget : NSObject <NSMenuDelegate> {
    snapback::TrayCallbacks callbacks_;
}
- (void)setCallbacks:(snapback::TrayCallbacks)callbacks;
- (void)menuItemClicked:(NSMenuItem*)sender;
@end

@implementation SnapbackTrayTarget

- (void)setCallbacks:(snapback::TrayCallbacks)callbacks {
    callbacks_ = std::move(callbacks);
}

- (void)menuItemClicked:(NSMenuItem*)sender {
    // The tag carries the shared command id, so this uses the same mapping as Win32.
    switch (snapback::tray_action_for(static_cast<unsigned int>(sender.tag))) {
        case snapback::TrayAction::Show:
            if (callbacks_.on_show) callbacks_.on_show();
            break;
        case snapback::TrayAction::Quit:
            if (callbacks_.on_quit) callbacks_.on_quit();
            break;
        case snapback::TrayAction::PauseRecording:
            if (callbacks_.on_pause_recording) callbacks_.on_pause_recording();
            break;
        case snapback::TrayAction::ResumeRecording:
            if (callbacks_.on_resume_recording) callbacks_.on_resume_recording();
            break;
        case snapback::TrayAction::SnoozeAlerts:
            if (callbacks_.on_snooze_alerts) callbacks_.on_snooze_alerts();
            break;
        case snapback::TrayAction::ResumeAlerts:
            if (callbacks_.on_resume_alerts) callbacks_.on_resume_alerts();
            break;
        case snapback::TrayAction::None:
            break;
    }
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    [menu removeAllItems];
    const auto status =
        callbacks_.recording_status ? callbacks_.recording_status() : snapback::RecordingStatus{};
    for (const snapback::TrayMenuEntry& entry : snapback::tray_menu_entries(status)) {
        if (snapback::tray_menu_entry_is_separator(entry)) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* item = [[[NSMenuItem alloc] initWithTitle:@(entry.label)
            action:@selector(menuItemClicked:) keyEquivalent:@""] autorelease];
        item.target = self;
        item.tag = static_cast<NSInteger>(entry.command_id);
        item.enabled = YES;
        [menu addItem:item];
    }
}

@end

namespace snapback {
namespace {

class MacTray final : public Tray {
public:
    ~MacTray() override {
        if (status_item_) {
            [[NSStatusBar systemStatusBar] removeStatusItem:status_item_];
            [status_item_ release];
        }
        [target_ release];
    }

    bool install(TrayCallbacks callbacks) override {
        // AppKit is main-thread-only. Returning instead of asserting keeps a mis-wired
        // caller from taking the process down, and the missing menu bar item is a loud
        // enough symptom on its own.
        if (![NSThread isMainThread]) return false;

        if (!target_) target_ = [[SnapbackTrayTarget alloc] init];
        [target_ setCallbacks:std::move(callbacks)];

        if (!status_item_) {
            // Retained for the process lifetime; the destructor removes it first.
            status_item_ = [[[NSStatusBar systemStatusBar]
                statusItemWithLength:NSVariableStatusItemLength] retain];
            // A text glyph: no bundle yet to carry an icon. `button` is nil only before 10.10.
            status_item_.button.title = @"◎";
            status_item_.button.toolTip = @"Snapback";
        }

        NSMenu* menu = [[[NSMenu alloc] initWithTitle:@"Snapback"] autorelease];
        menu.autoenablesItems = NO;
        menu.delegate = target_;
        status_item_.menu = menu;
        // Installed only if there is a clickable button.
        return status_item_ != nil && status_item_.button != nil;
    }

    // Unimplemented until the app is bundled; see the file header. Click routing is inert here.
    bool show_notification(const NotificationPayload&, AlertEvent, std::int64_t) override {
        return false;
    }

private:
    NSStatusItem* status_item_ = nil;
    SnapbackTrayTarget* target_ = nil;
};

}  // namespace

Tray& Tray::instance() {
    static MacTray tray;
    return tray;
}

}  // namespace snapback

#endif  // __APPLE__
