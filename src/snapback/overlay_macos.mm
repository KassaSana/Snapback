// macOS overlay: a borderless, always-on-top, non-activating NSPanel in the top-right, driven
// by the main run loop webview already runs.
//
// 1. NonactivatingPanel, so the card never takes the keystroke the user is typing. 2.
// hidesOnDeactivate = NO, or the card would hide whenever the app is not frontmost. 3.
// FullScreenAuxiliary + CanJoinAllSpaces, so it draws over full-screen apps and follows
//    Spaces.
// 4. The content view hit-tests as itself, so the text field cannot swallow click-to-dismiss
//    (dismissal is the tracker's only exit from Recovering).
#if defined(__APPLE__)

#include "snapback/overlay.hpp"

#import <Cocoa/Cocoa.h>

#include <functional>
#include <utility>

namespace {

// Matches the Windows card: RGB(24,24,32) on RGB(235,235,245).
constexpr double kCardRed = 24.0 / 255.0;
constexpr double kCardGreen = 24.0 / 255.0;
constexpr double kCardBlue = 32.0 / 255.0;
constexpr double kTextLevel = 235.0 / 255.0;

// Same 9s self-dismiss as the Windows overlay, and the same click-to-dismiss alongside it.
constexpr double kAutoDismissSeconds = 9.0;

// Windows pads the card 20pt horizontally and 18pt vertically; keep the text in the same
// place so the two platforms read as one product.
constexpr double kPadX = 20.0;
constexpr double kPadY = 18.0;

}  // namespace

// The card's background and its click target.
@interface SnapbackOverlayView : NSView
- (void)autoDismiss:(NSTimer*)timer;
@end

@implementation SnapbackOverlayView

- (NSView*)hitTest:(NSPoint)point {
    // Deliberately not the default deepest-subview walk: the whole card is one dismiss
    // button, so the label must not be allowed to consume the click. See note 4 above.
    return NSPointInRect(point, self.frame) ? self : nil;
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    snapback::Overlay::instance().dismiss();
}

- (void)autoDismiss:(NSTimer*)timer {
    (void)timer;
    snapback::Overlay::instance().dismiss();
}

@end

namespace snapback {
namespace {

class MacOverlay final : public Overlay {
public:
    ~MacOverlay() override {
        [timer_ invalidate];
        [timer_ release];
        // The panel owns its content view; releasing it releases the label too.
        [panel_ close];
        [panel_ release];
    }

    void show(const SnapbackPayload& payload) override {
        // AppKit is main-thread-only. main.cpp marshals through webview.dispatch, so this
        // guard is a backstop; returning rather than asserting keeps a mis-wired caller
        // from taking the process down over a missed nudge.
        if (![NSThread isMainThread]) return;

        ensure_panel();
        if (!panel_) return;

        label_.stringValue = @(overlay_text(payload).c_str());

        // mainScreen is the screen holding the active window, i.e. the one the user is
        // looking at — not necessarily the primary display. That is the right target: the
        // card is a nudge back to work, so it belongs where the user's eyes already are.
        NSScreen* screen = [NSScreen mainScreen] ?: [[NSScreen screens] firstObject];
        if (screen) {
            const NSRect visible = screen.visibleFrame;
            // monitor_pos.y = 0 so the y is a distance below the work area's top, which
            // cocoa_origin_y takes. visibleFrame clears the menu bar and Dock.
            const ScreenPoint top_down =
                top_right_position({static_cast<int>(NSMinX(visible)), 0},
                                   {static_cast<int>(NSWidth(visible)),
                                    static_cast<int>(NSHeight(visible))},
                                   kOverlayWidth, kScreenMargin);
            [panel_ setFrameOrigin:NSMakePoint(top_down.x,
                                               cocoa_origin_y(static_cast<int>(NSMaxY(visible)),
                                                              top_down.y, kOverlayHeight))];
        }

        // orderFrontRegardless, not makeKeyAndOrderFront: present without activating.
        [panel_ orderFrontRegardless];

        arm_timer();
    }

    void dismiss() override {
        if (![NSThread isMainThread]) return;

        // Fire on_dismiss_ only for a card that was actually visible.
        const bool was_visible = panel_ && panel_.isVisible;
        cancel_timer();
        [panel_ orderOut:nil];
        if (was_visible && on_dismiss_) on_dismiss_();
    }

    // Stored, not wired: the panel has no hit regions yet. The web UI's "Take me back" covers
    // it.
    void set_action_callback(std::function<bool()> on_action) override {
        on_action_ = std::move(on_action);
    }

    void set_dismiss_callback(std::function<void()> on_dismiss) override {
        on_dismiss_ = std::move(on_dismiss);
    }

private:
    void ensure_panel() {
        if (panel_) return;

        const NSRect frame = NSMakeRect(0, 0, kOverlayWidth, kOverlayHeight);
        panel_ = [[NSPanel alloc]
            initWithContentRect:frame
                      styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        // Without this, orderOut: on a closed panel would release it out from under us —
        // there is no ARC here and the panel is meant to live for the process.
        panel_.releasedWhenClosed = NO;
        panel_.level = NSStatusWindowLevel;  // above ordinary windows, like HWND_TOPMOST
        panel_.hidesOnDeactivate = NO;       // see note 2 in the file header
        panel_.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                    NSWindowCollectionBehaviorFullScreenAuxiliary |
                                    NSWindowCollectionBehaviorStationary;
        // The rounded corners are drawn by the content view's layer, so the panel itself
        // must not paint a square background behind them.
        panel_.opaque = NO;
        panel_.backgroundColor = [NSColor clearColor];

        SnapbackOverlayView* view =
            [[[SnapbackOverlayView alloc] initWithFrame:frame] autorelease];
        view.wantsLayer = YES;
        view.layer.backgroundColor =
            [NSColor colorWithSRGBRed:kCardRed green:kCardGreen blue:kCardBlue alpha:1.0].CGColor;
        view.layer.cornerRadius = 12.0;
        panel_.contentView = view;
        view_ = view;

        label_ = [[[NSTextField alloc]
            initWithFrame:NSMakeRect(kPadX, kPadY, kOverlayWidth - 2 * kPadX,
                                     kOverlayHeight - 2 * kPadY)] autorelease];
        label_.editable = NO;
        label_.selectable = NO;  // a selectable field would show an I-beam over a button
        label_.bordered = NO;
        label_.drawsBackground = NO;
        label_.textColor = [NSColor colorWithSRGBRed:kTextLevel
                                               green:kTextLevel
                                                blue:245.0 / 255.0
                                               alpha:1.0];
        label_.font = [NSFont systemFontOfSize:13.0];
        label_.cell.wraps = YES;
        label_.maximumNumberOfLines = 0;
        [view addSubview:label_];
    }

    void arm_timer() {
        cancel_timer();
        // Re-armed on every show(), so a second snapback arriving while a card is up gets
        // the full dwell time rather than the remainder of the first one's.
        timer_ = [[NSTimer scheduledTimerWithTimeInterval:kAutoDismissSeconds
                                                   target:view_
                                                 selector:@selector(autoDismiss:)
                                                 userInfo:nil
                                                  repeats:NO] retain];
    }

    void cancel_timer() {
        [timer_ invalidate];
        [timer_ release];
        timer_ = nil;
    }

    NSPanel* panel_ = nil;
    SnapbackOverlayView* view_ = nil;  // owned by panel_.contentView
    NSTextField* label_ = nil;         // owned by view_
    NSTimer* timer_ = nil;
    std::function<void()> on_dismiss_;
    std::function<bool()> on_action_;
};

}  // namespace

Overlay& Overlay::instance() {
    static MacOverlay overlay;
    return overlay;
}

}  // namespace snapback

#endif  // __APPLE__
