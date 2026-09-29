// No-op Tray for platforms without a native one (Linux). show_notification and install return
// false so callers know nothing was shown and do not hide the window into a missing tray.
// Guarded so it cannot collide with a native Tray::instance().
#if !defined(_WIN32) && !defined(__APPLE__)

#include "app/tray.hpp"

namespace snapback {
namespace {

class NoopTray final : public Tray {
public:
    // Callbacks are stored, not wired: there is no menu. false: there is no icon.
    bool install(TrayCallbacks callbacks) override {
        callbacks_ = std::move(callbacks);
        return false;
    }

    // false = "the OS did not accept this", which is accurate: there is no tray icon to
    // hang a notification off. Do not return true here — callers may later start trusting
    // it to decide whether to fall back to an in-app toast.
    bool show_notification(const NotificationPayload&, AlertEvent, std::int64_t) override {
        return false;
    }

private:
    TrayCallbacks callbacks_;
};

}  // namespace

Tray& Tray::instance() {
    static NoopTray tray;
    return tray;
}

}  // namespace snapback

#endif  // !_WIN32 && !__APPLE__
