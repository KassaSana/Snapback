// No-op Overlay for platforms without a native one (Linux). Snapbacks still reach the user
// through the web UI, whose Dismiss button calls `dismiss_snapback` -- the tracker's only exit
// from Recovering on these platforms. Guarded so it cannot collide with a native instance().
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

    // Stored, never invoked: there is no card. The web UI's "Take me back" uses IPC.
    void set_action_callback(std::function<bool()> on_action) override {
        on_action_ = std::move(on_action);
    }

private:
    std::function<void()> on_dismiss_;
    std::function<bool()> on_action_;
};

}  // namespace

Overlay& Overlay::instance() {
    static NoopOverlay overlay;
    return overlay;
}

}  // namespace snapback

#endif  // !_WIN32 && !__APPLE__
