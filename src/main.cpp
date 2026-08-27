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
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "app/activation_channel.hpp"
#include "app/commands.hpp"
#include "app/data_import.hpp"
#include "app/frontend_assets.hpp"
#include "app/ipc_shim.hpp"
#include "app/webview_origin.hpp"
#include "app/mac_ui.hpp"
#include "app/notification.hpp"
#include "app/single_instance.hpp"
#include "app/state.hpp"
#include "app/training_deploy.hpp"
#include "app/tray.hpp"
#include "app/window_lifecycle.hpp"
#include "capture/permissions.hpp"

#include "engine/onnx_model.hpp"
#include "snapback/overlay.hpp"
#include "storage/storage.hpp"
#include "util/logger.hpp"
#include "app/uninstall.hpp"
#include "util/private_dir.hpp"

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

// Roadmap 8.13. The choice of *where* is now separate from the environment lookups, so the
// fallback rule is testable. The old version ended `return temp_directory_path() / "snapback"`
// on both platforms: the same predictable path for every account, in a directory every local
// account can write to. That produced a working app quietly recording window titles somewhere
// anyone could read, with nothing said about it.
snapback::DataDirChoice app_data_dir() {
    const auto override_dir = env_var("SNAPBACK_DATA_DIR").value_or("");
#if defined(_WIN32)
    // %APPDATA% first, %LOCALAPPDATA% second: both are per-user by default, and the second is
    // where a roaming-profile machine puts data that should not roam.
    auto profile = env_var("APPDATA");
    if (!profile) profile = env_var("LOCALAPPDATA");
    const std::filesystem::path home =
        profile ? std::filesystem::path(*profile) / "snapback" : std::filesystem::path{};
#else
    const auto home_var = env_var("HOME");
    const std::filesystem::path home =
        home_var ? std::filesystem::path(*home_var) / ".snapback" : std::filesystem::path{};
#endif
    return snapback::choose_data_dir(override_dir.empty() ? std::filesystem::path{}
                                                          : std::filesystem::path(override_dir),
                                     home);
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

void run_tray_action(snapback::Logger& logger, const char* action,
                     const std::function<void()>& fn) noexcept {
    try {
        fn();
    } catch (const std::exception& error) {
        logger.warn(std::string("tray: ") + action + " failed: " + error.what());
    } catch (...) {
        logger.warn(std::string("tray: ") + action + " failed with an unknown error");
    }
}

snapback::RecordingStatus tray_recording_status(snapback::AppState& state,
                                                snapback::Logger& logger) noexcept {
    try {
        return state.recording_status();
    } catch (const std::exception& error) {
        logger.warn(std::string("tray: recording status failed: ") + error.what());
    } catch (...) {
        logger.warn("tray: recording status failed with an unknown error");
    }
    return snapback::RecordingStatus{snapback::RecordingState::Blocked, 0};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace snapback;

    // Roadmap 9.5. `snapback --purge` removes everything this app created and exits. The
    // Windows uninstaller runs it before deleting the binary, which is the only moment both
    // the data directory and something that knows where it is still exist. It prints what went
    // and what did not, because "most of it" must never be reported as "all of it" for an
    // application that recorded window titles.
    const bool purge = argc > 1 && std::string(argv[1]) == "--purge";

    // Roadmap 8.13. Fail closed rather than record somewhere shared. There is no safe
    // automatic answer when the user has no profile directory, so there is no automatic answer.
    const auto chosen = app_data_dir();
    if (!chosen.ok) {
        std::cerr << chosen.reason << '\n';
        return 1;
    }
    const auto data_dir = chosen.path;

    if (purge) {
        const auto removed = purge_app_data(data_dir);
        for (const auto& gone : removed.deleted) {
            std::cout << "removed: " << gone << "\n";
        }
        for (const auto& kept : removed.failed) {
            std::cerr << "could not remove: " << kept << "\n";
        }
        // Non-zero on a partial purge so an uninstaller can surface it rather than claiming a
        // clean removal it did not achieve.
        return removed.complete() ? 0 : 1;
    }

    // Owner-only from the moment it exists: `mkdir(0700)` rather than create-then-chmod, which
    // would leave the directory readable for the interval between the two calls.
    const auto privacy = prepare_private_dir(data_dir);
    if (!privacy.ok) {
        std::cerr << privacy.reason << '\n';
        return 1;
    }

    // Acquire this before opening even the rotating log: a losing process must not rotate
    // the live instance's file out from under it. The lock is tied to this object's OS
    // handle, so crashes release it automatically and a stale lock file is harmless.
    auto instance_guard = SingleInstanceGuard::acquire(data_dir / "snapback.lock");
    if (!instance_guard.acquired()) {
        // Roadmap 9.15. Losing the lock is not the end of this launch's job: the user asked to
        // see Snapback, and the running instance may be hidden in the tray with no window on
        // screen. Ask it to come forward before saying anything.
        //
        // This stays above `Storage::open` deliberately. A process that has opened no database,
        // started no capture and installed no tray is one whose only remaining act is to exit,
        // so blocking its only thread on the ack is the simplest correct thing it can do — and
        // the ordering is what keeps 9.8's "one database owner" true while it does.
        if (instance_guard.status() == SingleInstanceStatus::AlreadyRunning) {
            const auto activation = request_activation(data_dir);
            // Silence on success. The window is now in front of the user, and a console line
            // nothing owns is not an improvement on that.
            if (activation == ActivationResult::Activated) return 0;
            std::cerr << instance_guard.message() << " (could not raise its window: "
                      << activation_result_as_str(activation) << ")\n";
            return 0;
        }
        std::cerr << instance_guard.message() << '\n';
        return 1;
    }

    // One leveled logger for the process, writing to a rotating file next to
    // the DB instead of the console (nothing owns a terminal once this ships as a GUI
    // app). Falls back to stderr if the file can't be opened, so a bad log path degrades
    // instead of going silent. Level is overridable via SNAPBACK_LOG (e.g. "debug"),
    // Keep startup logging configurable without coupling it to the UI.
    RotatingFileStream log_file(data_dir / "snapback.log");
    Logger logger(pick_startup_log_sink(log_file, std::cerr),
                  level_from_string(env_var("SNAPBACK_LOG").value_or("")));

    const auto model_recovery =
        training_deploy::recover_model_deployment_for_startup(data_dir);
    if (!model_recovery.ok) {
        logger.warn(std::string("model deployment recovery degraded: ") +
                    model_recovery.message);
    }
    try {
#if defined(SNAPBACK_ONNX)
        if (auto model = OnnxModel::resolve_model_path(data_dir)) {
            OnnxModel::instance().init(*model);
        }
#endif
    } catch (const std::exception& error) {
        logger.warn(std::string("optional model load failed: ") + error.what());
    }

    // Observability: log the startup sequence, one line per step, at INFO. This exists
    // because the log file was empty through an entire successful startup — so when the
    // macOS window came up blank there was nothing on disk to say how far we got or what
    // the webview was pointed at. Capture and the webview are the two things that cannot
    // be verified headlessly, which makes them exactly the two that must narrate.
    logger.info("snapback " SNAPBACK_VERSION " starting");
    logger.info("data dir: " + data_dir.string());
    // Roadmap 8.13. Said after the logger exists, because the repair happens before it does.
    // An entry that could not be tightened is reported rather than swallowed: claiming
    // "secured" over a file we failed to touch is the specific failure the item names.
    for (const auto& repaired : privacy.repaired) {
        logger.info("restricted permissions on " + repaired);
    }
    for (const auto& exposed : privacy.unprotected) {
        logger.warn("still readable by other accounts: " + exposed);
    }

    // Roadmap 9.14. Before anything opens the database, apply an import the user staged in a
    // previous run. It has to be here: the swap replaces the file, so it cannot happen while a
    // connection is open, and 9.8's lock means the running app is always that connection.
    if (const auto imported = apply_staged_import(data_dir / "focoflow.db", &logger)) {
        if (imported->ok) {
            logger.info("startup: applied staged import — " + imported->message);
        } else {
            // Not fatal. The apply is written so that a failure leaves the previous database in
            // place, so the right move is to start normally and say what happened.
            logger.error("startup: staged import failed — " + imported->message);
        }
    }

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
    auto state = std::make_unique<AppState>(
        std::move(*storage), data_dir, &logger, nullptr,
        training_deploy::to_model_deployment_health(model_recovery));
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

    webview::webview w(/*debug=*/kWebviewDebugEnabled, nullptr);
    // This guard is declared after the webview, so exception unwinding stops
    // capture and event dispatch before the webview itself is destroyed.
    EngineLifetime engine_lifetime(*state);
    w.set_title("Snapback");
    w.set_size(1100, 760, WEBVIEW_HINT_NONE);

    // Resolve the trusted document before binding commands. Roadmap 8.14: the shim and every
    // native bind share one canonical URL and one per-launch capability token.
#if defined(NDEBUG)
    const auto frontend_url = resolve_frontend_url(executable_dir(), std::nullopt, false, false);
#else
    const auto frontend_url =
        resolve_frontend_url(executable_dir(), env_var("SNAPBACK_FRONTEND_URL"), true);
#endif
    const auto trusted_url = canonical_document_url(frontend_url);
    const auto capability_token = generate_capability_token();

    // Inject the IPC shim BEFORE any page script runs (init scripts run on every navigation,
    // ahead of the bundle), then register the command binds it calls.
    w.init(build_ipc_shim_script(trusted_url, capability_token, kWebviewDebugEnabled));
    register_commands(w, *state, data_dir, capability_token);

    // System tray (Phase 8): left-click/double-click or the "Show" menu item brings the
    // window forward; "Quit" ends the run loop. Both branches read the native window
    // handle out of the webview once, here on the UI thread, because that is the only
    // thread either platform's window APIs may be touched from.
    // Close-to-tray (Roadmap 9.15): closing the window hides it instead of terminating the app.
    //
    // How the window comes forward is written once per platform and shared by everything that
    // needs it: the tray's Show item, and 9.15's activation channel below. Two copies would
    // drift, and only one of them would be the one anybody actually clicks.
    std::function<void()> raise_window;
#if defined(_WIN32)
    if (auto win = w.window(); win.ok()) {
        HWND main_hwnd = reinterpret_cast<HWND>(win.value());
        enable_close_to_tray(main_hwnd);
        raise_window = [main_hwnd] {
            ShowWindow(main_hwnd, SW_SHOW);
            SetForegroundWindow(main_hwnd);
        };
        TrayCallbacks callbacks;
        callbacks.on_show = raise_window;
        callbacks.on_quit = [&w, main_hwnd] {
            prepare_app_exit(main_hwnd);
            w.terminate();
        };
        callbacks.recording_status = [state = state.get(), &logger] {
            return tray_recording_status(*state, logger);
        };
        callbacks.on_pause_recording = [state = state.get(), &logger] {
            run_tray_action(logger, "pause recording", [state] { state->pause_privately_for(0); });
        };
        callbacks.on_resume_recording = [state = state.get(), &logger] {
            run_tray_action(logger, "resume recording",
                            [state] { state->resume_from_private_pause(); });
        };
        callbacks.on_snooze_alerts = [state = state.get(), &logger] {
            run_tray_action(logger, "snooze alerts",
                            [state] { state->snooze_alerts_for(kDefaultAlertSnoozeMins); });
        };
        callbacks.on_resume_alerts = [state = state.get(), &logger] {
            run_tray_action(logger, "resume alerts", [state] { state->resume_alerts(); });
        };
        Tray::instance().install(std::move(callbacks));
    }
#elif defined(__APPLE__)
    // webview's window() hands back the NSWindow* as an opaque void*; mac_ui.mm is where
    // it becomes AppKit again, so main.cpp stays plain C++ (see mac_ui.hpp).
    if (auto win = w.window(); win.ok()) {
        void* main_window = win.value();
        enable_close_to_tray(main_window);
        raise_window = [main_window] { mac::bring_window_to_front(main_window); };
        TrayCallbacks callbacks;
        callbacks.on_show = raise_window;
        callbacks.on_quit = [&w, main_window] {
            prepare_app_exit(main_window);
            w.terminate();
        };
        callbacks.recording_status = [state = state.get(), &logger] {
            return tray_recording_status(*state, logger);
        };
        callbacks.on_pause_recording = [state = state.get(), &logger] {
            run_tray_action(logger, "pause recording", [state] { state->pause_privately_for(0); });
        };
        callbacks.on_resume_recording = [state = state.get(), &logger] {
            run_tray_action(logger, "resume recording",
                            [state] { state->resume_from_private_pause(); });
        };
        callbacks.on_snooze_alerts = [state = state.get(), &logger] {
            run_tray_action(logger, "snooze alerts",
                            [state] { state->snooze_alerts_for(kDefaultAlertSnoozeMins); });
        };
        callbacks.on_resume_alerts = [state = state.get(), &logger] {
            run_tray_action(logger, "resume alerts", [state] { state->resume_alerts(); });
        };
        Tray::instance().install(std::move(callbacks));
    }
#endif

    // Roadmap 9.15. The owner's half of the activation channel, started once the window exists
    // and kept alive by this scope until `w.run()` returns.
    //
    // `on_activate` arrives on the listener's own thread, so it does nothing but hand the work
    // to the UI thread — the same rule the emit hook below follows, and for the same reason:
    // both platforms' window APIs may only be touched from the thread that owns the run loop.
    // Raising the window directly from here would be the ordinary cross-thread UI bug, arriving
    // only on a second launch and therefore almost never on a developer's machine.
    std::optional<ActivationListener> activation_listener;
    if (raise_window) {
        activation_listener = ActivationListener::start(data_dir, [&w, raise_window, &logger] {
            logger.info("activation: a second launch asked for the window");
            w.dispatch([raise_window] { raise_window(); });
        });
        if (!activation_listener) {
            // Not fatal, and deliberately not silent. Refusing to start because a convenience
            // channel failed would trade a small annoyance for a total outage; saying nothing
            // would leave "double-click does nothing" with no evidence anywhere.
            logger.warn("activation channel unavailable — a second launch cannot raise this "
                        "window and will report that it could not");
        } else {
            logger.info("activation channel listening on " + activation_listener->endpoint());
        }
    }

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
            // Roadmap 2.16. Which channels fire is decided in app/alert_routing.hpp and
            // arrives on the payload; this reads flags and decides nothing.
            //
            // The previous comment here claimed the toast was needed because "the overlay
            // alone can't" reach a user whose app window is not focused. That was wrong:
            // the overlay is created WS_EX_TOPMOST | WS_EX_NOACTIVATE and shown with
            // SW_SHOWNOACTIVATE on the foreground window's monitor (snapback/
            // overlay_windows.cpp), so it is always-on-top and never steals focus -- it
            // reaches the user regardless. The toast was duplication that additionally
            // copied the summary, which may name a file or a project, into OS notification
            // history. Hence the default is now the overlay alone, with "both" one
            // preference away.
            if (ev == "snapback") {
                try {
                    const auto parsed = nlohmann::json::parse(payload);
                    const auto snap = parsed.get<SnapbackPayload>();
                    // A missing delivery block means a bug, not an old payload -- the tick
                    // that emits it is in this same binary. Falling back to the shipped
                    // defaults keeps the app audible while that bug is found; an all-false
                    // route would silently stop interrupting and look like working software.
                    const auto route =
                        parsed.contains("delivery")
                            ? parsed["delivery"].get<AlertRoute>()
                            : route_alert(AlertEvent::Snapback, AlertDeliverySettings{}, 0,
                                          std::nullopt);
                    if (route.channels.overlay) Overlay::instance().show(snap);
                    if (route.channels.native) {
                        Tray::instance().show_notification(
                            build_snapback_notification(snap, route.preview));
                    }
                } catch (...) {
                    // A malformed payload must never take down the UI thread.
                }
            }
            // The hyperfocus nudge defaults to a toast and no overlay: an overlay here would
            // interrupt exactly the deep work the guardrail is trying to protect. Roadmap
            // 2.16 makes that a preference rather than a rule, so it is read, not assumed.
            if (ev == "hyperfocus") {
                try {
                    const auto parsed = nlohmann::json::parse(payload);
                    const auto minutes = parsed.at("minutes").get<std::uint64_t>();
                    const auto route =
                        parsed.contains("delivery")
                            ? parsed["delivery"].get<AlertRoute>()
                            : route_alert(AlertEvent::Hyperfocus, AlertDeliverySettings{}, 0,
                                          std::nullopt);
                    if (route.channels.native) {
                        Tray::instance().show_notification(
                            build_hyperfocus_notification(minutes, route.preview));
                    }
                    if (route.channels.overlay) {
                        SnapbackPayload nudge;
                        nudge.summary = build_hyperfocus_notification(minutes).body;
                        Overlay::instance().show(nudge);
                    }
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
