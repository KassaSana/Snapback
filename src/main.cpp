// Entry point for the native application.
//
// Boot order:
//   1. resolve the app data directory
//   2. acquire the single-instance lock
//   3. optionally load the ONNX model
//   4. open SQLite storage
//   5. build AppState and start the engine
//   6. create the webview, bind IPC, load the React build, run the loop
#include "app/webview_compat.hpp"  // webview.h + X11 macro scrub — never include webview.h raw

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "app/commands.hpp"
#include "app/frontend_assets.hpp"
#include "app/ipc_shim.hpp"
#include "app/notification.hpp"
#include "app/single_instance.hpp"
#include "app/state.hpp"
#include "app/training_deploy.hpp"
#include "app/tray.hpp"
#include "capture/permissions.hpp"
#include "engine/onnx_model.hpp"
#include "snapback/overlay.hpp"
#include "storage/storage.hpp"
#include "util/logger.hpp"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath — see executable_dir()
#endif

namespace {

class EngineLifetime {
public:
    explicit EngineLifetime(snapback::AppState& state) : state_(&state) {}
    ~EngineLifetime() noexcept { stop(); }

    void stop() noexcept {
        if (!state_) return;
        state_->set_emit_hook(nullptr);
        state_->stop_engine();
        state_ = nullptr;
    }

private:
    snapback::AppState* state_;
};

std::optional<std::string> env_var(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return std::nullopt;
    std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
    return std::string(owned.get());
#else
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
#endif
}

std::filesystem::path app_data_dir() {
    if (auto override_dir = env_var("SNAPBACK_DATA_DIR")) {
        return std::filesystem::path(*override_dir);
    }
#if defined(_WIN32)
    if (auto app_data = env_var("APPDATA")) return std::filesystem::path(*app_data) / "snapback";
    if (auto local_app_data = env_var("LOCALAPPDATA")) {
        return std::filesystem::path(*local_app_data) / "snapback";
    }
    return std::filesystem::temp_directory_path() / "snapback";
#else
    if (auto home = env_var("HOME")) return std::filesystem::path(*home) / ".snapback";
    return std::filesystem::temp_directory_path() / "snapback";
#endif
}

// The directory the running binary lives in — NOT the working directory. Those coincide
// often enough during development to hide a bug: this function was Windows-only, and every
// other platform fell through to `current_path()`. Launched as `./build-app/snapback` from
// the repo root, that made the app load the *source* `frontend/index.html` (Vite's dev
// template, whose only script is `/src/main.tsx`) instead of the built bundle next to the
// binary — a silently blank window, and one that never trips the missing-bundle check
// because a file really is there.
std::filesystem::path executable_dir() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (len == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (len > 0) {
        buffer.resize(len);
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(__APPLE__)
    // _NSGetExecutablePath writes the path used to launch the process; if the buffer is too
    // small it reports the required size and fails, so retry once at that size.
    std::uint32_t size = 4096;
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.assign(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) return std::filesystem::current_path();
    }
    buffer.resize(std::strlen(buffer.c_str()));
    // The launch path may be relative or contain symlinks; canonical() gives the real dir.
    std::error_code error;
    const auto resolved = std::filesystem::canonical(buffer, error);
    return (error ? std::filesystem::path(buffer) : resolved).parent_path();
#else
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return self.parent_path();
#endif
    return std::filesystem::current_path();
}

}  // namespace

int main() {
    using namespace snapback;

    const auto data_dir = app_data_dir();
    std::filesystem::create_directories(data_dir);

    // Acquire this before opening even the rotating log: a losing process must not rotate
    // the live instance's file out from under it. The lock is tied to this object's OS
    // handle, so crashes release it automatically and a stale lock file is harmless.
    auto instance_guard = SingleInstanceGuard::acquire(data_dir / "snapback.lock");
    if (!instance_guard.acquired()) {
        std::cerr << instance_guard.message() << '\n';
        return instance_guard.status() == SingleInstanceStatus::AlreadyRunning ? 0 : 1;
    }

    // One leveled logger for the process, writing to a rotating file next to
    // the DB instead of the console (nothing owns a terminal once this ships as a GUI
    // app). Falls back to stderr if the file can't be opened, so a bad log path degrades
    // instead of going silent. Level is overridable via SNAPBACK_LOG (e.g. "debug"),
    // Keep startup logging configurable without coupling it to the UI.
    RotatingFileStream log_file(data_dir / "snapback.log");
    Logger logger(pick_startup_log_sink(log_file, std::cerr),
                  level_from_string(env_var("SNAPBACK_LOG").value_or("")));

    try {
        training_deploy::recover_model_deployment(data_dir);
#if defined(SNAPBACK_ONNX)
        if (auto model = OnnxModel::resolve_model_path(data_dir)) {
            OnnxModel::instance().init(*model);
        }
#endif
    } catch (const std::exception& error) {
        logger.error(std::string("model deployment recovery failed: ") + error.what());
        return 1;
    }

    // Observability: log the startup sequence, one line per step, at INFO. This exists
    // because the log file was empty through an entire successful startup — so when the
    // macOS window came up blank there was nothing on disk to say how far we got or what
    // the webview was pointed at. Capture and the webview are the two things that cannot
    // be verified headlessly, which makes them exactly the two that must narrate.
    logger.info("snapback " SNAPBACK_VERSION " starting");
    logger.info("data dir: " + data_dir.string());

    try {
        auto storage = Storage::open(data_dir, &logger);
        if (!storage) {
            logger.error("failed to open storage at startup");
            return 1;
        }
        logger.info("storage opened: " + (data_dir / "focoflow.db").string());

    // Keep AppState heap-owned so callbacks registered below can borrow one stable
    // process-lifetime address. RingBuffer independently heap-backs its 64K slots, so
    // AppState itself remains stack-friendly.
    auto state = std::make_unique<AppState>(std::move(*storage), data_dir, &logger);
    state->start_engine();

    // Read-only probe (never prompts), logged once at startup so the log answers the
    // question 0.3 exists to answer: did the OS actually let us capture? Without this, a
    // starved event tap and a working one look identical from outside.
    const auto permissions = check_capture_permissions(/*capture_running=*/true);
    logger.info(std::string("engine started; capture permission: ") +
                (permissions.capture_available ? "granted" : "DENIED") +
                ", probe confirmed: " + (permissions.capture_probe_confirmed ? "yes" : "no") +
                ", active window: " + (permissions.active_window_available ? "yes" : "no"));
    if (!permissions.capture_available) {
        logger.warn("capture permission missing — " + permissions.message);
    }

    // The overlay's own dismiss triggers (auto-timeout, click) have no other route back
    // into app state — ContextTracker::dismiss_recovery() is the only exit from
    // Recovering, so without this the tracker gets stuck after the first snapback of a
    // session and never fires another one. Registering here covers every dismiss path,
    // not just the IPC `dismiss_snapback` command.
    Overlay::instance().set_dismiss_callback(
        [state = state.get()] { state->dismiss_snapback(); });

    webview::webview w(/*debug=*/true, nullptr);
    // This guard is declared after the webview, so exception unwinding stops
    // capture and event dispatch before the webview itself is destroyed.
    EngineLifetime engine_lifetime(*state);
    w.set_title("Snapback");
    w.set_size(1100, 760, WEBVIEW_HINT_NONE);

    // Inject the IPC shim BEFORE any page script runs (init scripts run on
    // every navigation, ahead of the bundle), then register the command binds it calls.
    w.init(kIpcShim);
    register_commands(w, *state, data_dir);

    // System tray (Phase 8): left-click/double-click or the "Show" menu item brings the
    // window forward; "Quit" ends the run loop.
#if defined(_WIN32)
    if (auto win = w.window(); win.ok()) {
        HWND main_hwnd = reinterpret_cast<HWND>(win.value());
        Tray::instance().install(
            [main_hwnd] {
                ShowWindow(main_hwnd, SW_SHOW);
                SetForegroundWindow(main_hwnd);
            },
            [&w] { w.terminate(); });
    }
#endif

    // Host->frontend events: the engine tick runs off-thread, but webview.eval and the
    // Win32 overlay must run on the UI thread — so marshal via dispatch. Copy
    // event/payload by value; the tick's strings don't outlive the hook call.
    state->set_emit_hook([&w, state = state.get()](const char* event,
                                                   const std::string& payload,
                                                   AppState::ActivityEpoch activity_epoch) {
        std::string ev = event;
        w.dispatch([&w, state, ev, payload, activity_epoch] {
            // The engine thread only queues this closure. A delete command may execute on
            // the UI thread before the queue reaches us, so reject the stale tick here,
            // immediately before every user-visible side effect.
            if (!state->activity_epoch_is_current(activity_epoch)) return;
            emit(w, ev.c_str(), payload);
            // On the return-from-distraction edge, also pop the native overlay card and a
            // toast — the toast is the one that reaches the user when the
            // app window isn't focused, which is exactly when the overlay alone can't.
            if (ev == "snapback") {
                try {
                    const auto snap = nlohmann::json::parse(payload).get<SnapbackPayload>();
                    Overlay::instance().show(snap);
                    Tray::instance().show_notification(build_snapback_notification(snap));
                } catch (...) {
                    // A malformed payload must never take down the UI thread.
                }
            }
            // The hyperfocus nudge is a toast only — no overlay. An overlay here would
            // interrupt exactly the deep work the guardrail is trying to protect.
            if (ev == "hyperfocus") {
                try {
                    const auto minutes =
                        nlohmann::json::parse(payload).at("minutes").get<std::uint64_t>();
                    Tray::instance().show_notification(build_hyperfocus_notification(minutes));
                } catch (...) {
                }
            }
        });
    });

    // Manual-QA hook: SNAPBACK_OVERLAY_TEST=1 pops a sample overlay on launch so the
    // window can be eyeballed without staging a real distraction. No-op otherwise.
    if (env_var("SNAPBACK_OVERLAY_TEST")) {
        w.dispatch([] {
            SnapbackPayload demo;
            demo.summary = "Return to overlay_windows.cpp";
            demo.app_name = "Cursor";
            demo.file_hint = "overlay_windows.cpp";
            demo.distraction_duration_secs = 37;
            Overlay::instance().show(demo);
        });
    }

    // Manual-QA hook: SNAPBACK_NOTIFICATION_TEST=1 sends a sample native balloon through
    // the already-installed tray icon. This exercises the Win32 delivery path without
    // fabricating a distraction in the capture stream.
    if (env_var("SNAPBACK_NOTIFICATION_TEST")) {
        w.dispatch([] {
            Tray::instance().show_notification(build_distraction_notification("YouTube"));
        });
    }

    // Dev-only override: a release process must never let its launch environment redirect
    // the webview to arbitrary remote content, because that page receives the full IPC shim.
    // Demo/release path loads the bundled frontend copied next to snapback.exe.
#if defined(NDEBUG)
    const auto frontend_url = resolve_frontend_url(executable_dir(), std::nullopt, false, false);
#else
    const auto frontend_url =
        resolve_frontend_url(executable_dir(), env_var("SNAPBACK_FRONTEND_URL"), true);
#endif
    // Log the URL, not just the fact that we navigated. A malformed `file:////...` URL (the
    // POSIX four-slash bug) and a missing bundle both render an empty window silently; the
    // only difference is visible in this string.
    logger.info("navigating webview to: " + frontend_url);
    if (frontend_url == "about:blank") {
        logger.error("no frontend bundle next to the executable (" + executable_dir().string() +
                     "/frontend/index.html) — the window will be empty. Build it with "
                     "`npm run build` in frontend/ and rebuild.");
    }
    w.navigate(frontend_url);

    if (env_var("SNAPBACK_GUI_SESSION_SMOKE")) {
        w.dispatch([state = state.get(), data_dir, &w]() {
            const auto session = state->start_session("GUI session smoke", FocusMode::Normal);
            state->stop_session(session.session_id);
            std::ofstream marker(data_dir / "gui_session_smoke.ok");
            marker << session.session_id;
            w.terminate();
        });
    }

    w.run();

        engine_lifetime.stop();
        return 0;
    } catch (const std::exception& error) {
        logger.error(std::string("startup/runtime failure: ") + error.what());
        return 1;
    } catch (...) {
        logger.error("startup/runtime failure: unknown exception");
        return 1;
    }
}
