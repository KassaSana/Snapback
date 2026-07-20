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
// Replace with real implementations per platform: ROADMAP 3.1 (macOS NSPanel) and
// 3.2 (Linux X11/Wayland overlay).
#if !defined(_WIN32)

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

private:
    std::function<void()> on_dismiss_;
};

}  // namespace

Overlay& Overlay::instance() {
    static NoopOverlay overlay;
    return overlay;
}

}  // namespace snapback

#endif  // !_WIN32
