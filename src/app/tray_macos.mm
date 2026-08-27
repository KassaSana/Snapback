// macOS system tray: an NSStatusItem in the menu bar with an NSMenu built from the shared
// tray_menu_entries() model. Roadmap 3.1.
//
// Two things differ from the Windows tray and are worth stating, because both are places
// where copying tray_windows.cpp would have produced something subtly wrong.
//
// 1. There is no message-only window and no message pump of our own. Cocoa delivers menu
//    clicks through the responder chain on the main thread's run loop, which webview
//    already runs. That is why install() refuses to do anything off the main thread: an
//    NSStatusItem created on a worker thread is undefined behaviour, and the symptom is a
//    menu bar item that exists but never responds.
//
// 2. Notifications stay unimplemented on purpose. Posting one needs UNUserNotification-
//    Center, which needs a bundle identifier, which we do not have until Roadmap 3.3
//    packages the .app. show_notification() therefore keeps returning false — the same
//    contract NoopTray documents, for the same reason: a caller may start trusting the
//    return value to decide whether to fall back, and "true" here would be a lie.
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
    // The item's tag carries the shared command id, so this goes through exactly the same
    // tested mapping the Win32 WM_COMMAND path uses instead of switching on the title.
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
            // systemStatusBar owns the item, so retain it to keep our pointer valid for
            // the process lifetime; the destructor removes it before releasing.
            status_item_ = [[[NSStatusBar systemStatusBar]
                statusItemWithLength:NSVariableStatusItemLength] retain];
            // A text glyph, not an image: there is no icon resource to load until 3.3
            // gives us a bundle to carry one. `button` is nil only on pre-10.10 systems.
            status_item_.button.title = @"◎";
            status_item_.button.toolTip = @"Snapback";
        }

        NSMenu* menu = [[[NSMenu alloc] initWithTitle:@"Snapback"] autorelease];
        menu.autoenablesItems = NO;
        menu.delegate = target_;
        status_item_.menu = menu;
        // The status item is what the user clicks to get the window back, so its existence is
        // the honest answer to "did a tray install?" -- `button` is nil on systems too old to
        // draw one, and a menu hung off nothing is not a way back.
        return status_item_ != nil && status_item_.button != nil;
    }

    // Deliberately unimplemented until Roadmap 3.3 — see the file header.
    //
    // Roadmap 2.16's click routing is therefore inert here, and that is a consequence rather
    // than an omission: there is no toast to click. The remembered event/id are dropped for
    // the same reason. When 3.3 gives this a real UNUserNotificationCenter delivery, the
    // click handler goes in beside it and TrayCallbacks::on_notification_click is waiting.
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
