// No-op Tray for platforms without a native implementation (macOS, Linux).
//
// Same gap as overlay_stub.cpp: tray.hpp promises a no-op fallback "so the build stays
// green cross-platform", but `Tray::instance()` was only ever defined in
// tray_windows.cpp, which CMake adds only under if(WIN32). Linking the desktop app off
// Windows failed on the undefined symbol.
//
// The tray is pure convenience here (show-window / quit shortcuts), so a no-op costs the
// user nothing they cannot do from the main window. Notifications are the part that
// actually degrades: show_notification reports false so callers know the OS never saw it.
// Every current caller treats a notification as a best-effort nudge and ignores the
// result, which is why a false return is safe rather than a silent lie.
//
// Replace with real implementations per platform: ROADMAP 3.1 (macOS NSStatusItem) and
// 3.2 (Linux libappindicator).
#if !defined(_WIN32)

#include "app/tray.hpp"

namespace snapback {
namespace {

class NoopTray final : public Tray {
public:
    // Callbacks are stored, not wired: there is no menu to fire them. main.cpp keeps
    // working because quitting and showing the window are both reachable from the UI.
    void install(std::function<void()> on_show, std::function<void()> on_quit) override {
        on_show_ = std::move(on_show);
        on_quit_ = std::move(on_quit);
    }

    // false = "the OS did not accept this", which is accurate: there is no tray icon to
    // hang a notification off. Do not return true here — callers may later start trusting
    // it to decide whether to fall back to an in-app toast.
    bool show_notification(const NotificationPayload&) override { return false; }

private:
    std::function<void()> on_show_;
    std::function<void()> on_quit_;
};

}  // namespace

Tray& Tray::instance() {
    static NoopTray tray;
    return tray;
}

}  // namespace snapback

#endif  // !_WIN32
