// No-op Overlay for platforms without a native implementation (macOS, Linux).
//
// overlay.hpp promises that "instance() returns the per-platform implementation (a no-op
// where unimplemented, so the build stays green cross-platform)". That fallback was
// documented but never written, so `Overlay::instance()` existed only in
// overlay_windows.cpp — which CMake adds only under if(WIN32). The result: linking the
// desktop app on macOS or Linux failed on an undefined symbol. This file is that promise.
//
// Losing the native card is not the same as losing the feature. The snapback still
// reaches the user through the web UI: AppState emits the "snapback" event, the React
// side renders the note, and its Dismiss button calls the `dismiss_snapback` IPC command.
// That matters because ContextTracker's Recovering state has exactly one exit
// (dismiss_recovery), so a platform where nothing can dismiss would latch after the first
// snapback of a session. The IPC path keeps that exit reachable here.
//
// macOS now has a real one (overlay_macos.mm, ROADMAP 3.1); Linux is still to come
// (ROADMAP 3.2, X11/Wayland). The guard excludes both platforms that define
// Overlay::instance() elsewhere, so adding a native backend cannot produce a duplicate
// symbol even if CMake keeps listing this file.
//
// The paragraph above still describes Linux exactly: there, the web UI's Dismiss button
// remains the only exit from Recovering.
#if !defined(_WIN32) && !defined(__APPLE__)

#include "snapback/overlay.hpp"

namespace snapback {
namespace {

class NoopOverlay final : public Overlay {
public:
    void show(const SnapbackPayload&) override {}
    void dismiss() override {}

    // Store but never invoke it: with no native card there is no auto-dismiss timer and
    // no click to fire on. The web UI's Dismiss button drives AppState directly over IPC,
    // so state still unlatches — see the note above.
    void set_dismiss_callback(std::function<void()> on_dismiss) override {
        on_dismiss_ = std::move(on_dismiss);
    }

    // Roadmap 2.16. Stored and never invoked, for the same reason as the dismiss callback
    // above: there is no card, so there is no region to click. The web UI's own "Take me back"
    // button reaches restore_snapback_target over IPC, so the action itself is not lost here.
    void set_action_callback(std::function<void()> on_action) override {
        on_action_ = std::move(on_action);
    }

private:
    std::function<void()> on_dismiss_;
    std::function<void()> on_action_;
};

}  // namespace

Overlay& Overlay::instance() {
    static NoopOverlay overlay;
    return overlay;
}

}  // namespace snapback

#endif  // !_WIN32 && !__APPLE__
