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
#include "app/acceptance_harness.hpp"
#include "app/commands.hpp"
#include "app/data_import.hpp"
#include "app/events.hpp"
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

// Where the data lives. Fails closed rather than falling back to a shared temp directory anyone
// could read.
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

// The directory of the running binary, not the working directory -- the two coincide in
// development and hide a bug where the source index.html is loaded instead of the bundle.
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

// Returns whether `fn` completed; route_alert_click needs to know when an action threw.
bool run_tray_action(snapback::Logger& logger, const char* action,
                     const std::function<void()>& fn) noexcept {
    try {
        fn();
        return true;
    } catch (const std::exception& error) {
        logger.warn(std::string("tray: ") + action + " failed: " + error.what());
    } catch (...) {
        logger.warn(std::string("tray: ") + action + " failed with an unknown error");
    }
    return false;
}

// The event the frontend acts on when a native click chose a destination this side does not
// own. The alert id travels along for log correlation.
std::string dump_alert_action_event(snapback::AlertAction action, std::int64_t alert_id) {
    return nlohmann::json{{"action", snapback::alert_action_as_str(action)},
                          {"alertId", alert_id}}
        .dump();
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

    // `snapback --purge` removes everything this app created and exits; the Windows uninstaller
    // runs it. It prints what was and was not removed.
    const bool purge = argc > 1 && std::string(argv[1]) == "--purge";

    // Fail closed rather than record somewhere shared.
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

    // Acquire before opening the log, so a losing process cannot rotate the live instance's
    // file. The lock is tied to an OS handle, so a crash releases it.
    auto instance_guard = SingleInstanceGuard::acquire(data_dir / "snapback.lock");
    if (!instance_guard.acquired()) {
        // Ask the running instance (which may be hidden in the tray) to come forward, then
        // exit. Before Storage::open, so there is only ever one database owner.
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

    // One leveled logger writing to a rotating file next to the DB, falling back to stderr.
    // SNAPBACK_LOG overrides the level (e.g. "debug").
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

    // Log each startup step: capture and the webview cannot be verified headlessly, so the log
    // is how a blank window gets diagnosed.
    logger.info("snapback " SNAPBACK_VERSION " starting");
    logger.info("data dir: " + data_dir.string());
    // Reported after the logger exists; an entry that could not be tightened is said so.
    for (const auto& repaired : privacy.repaired) {
        logger.info("restricted permissions on " + repaired);
    }
    for (const auto& exposed : privacy.unprotected) {
        logger.warn("still readable by other accounts: " + exposed);
    }

    // Apply a staged import before anything opens the database: the swap replaces the file.
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

    // Heap-owned so callbacks below can borrow a stable address.
    auto state = std::make_unique<AppState>(
        std::move(*storage), data_dir, &logger, nullptr,
        training_deploy::to_model_deployment_health(model_recovery));
    state->start_engine();

    // Read-only probe (never prompts), logged once: did the OS let us capture?
    const auto permissions = check_capture_permissions(/*capture_running=*/true);
    logger.info(std::string("engine started; capture permission: ") +
                (permissions.capture_available ? "granted" : "DENIED") +
                ", probe confirmed: " + (permissions.capture_probe_confirmed ? "yes" : "no") +
                ", active window: " + (permissions.active_window_available ? "yes" : "no"));
    if (!permissions.capture_available) {
        logger.warn("capture permission missing — " + permissions.message);
    }

    // Route every overlay dismiss (timeout, click) back into state: dismiss_recovery() is the
    // tracker's only exit from Recovering.
    Overlay::instance().set_dismiss_callback(
        [state = state.get()] { state->dismiss_snapback(); });

    // The overlay's "Take me back" action is registered below, once the window exists.

    webview::webview w(/*debug=*/kWebviewDebugEnabled, nullptr);
    // This guard is declared after the webview, so exception unwinding stops
    // capture and event dispatch before the webview itself is destroyed.
    EngineLifetime engine_lifetime(*state);
    w.set_title("Snapback");
    w.set_size(1100, 760, WEBVIEW_HINT_NONE);

    // Resolve the trusted document first: the shim and every native bind share one canonical
    // URL and one per-launch capability token.
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

    // Only GUI-smoke builds can load an acceptance script, so the env var is never a capability
    // in a packaged binary.
    std::optional<std::string> acceptance_script;
#if defined(SNAPBACK_ENABLE_ACCEPTANCE_HARNESS)
    if (const auto path = env_var("SNAPBACK_ACCEPTANCE_SCRIPT")) {
        acceptance_script = load_acceptance_script(*path);
        w.init(*acceptance_script);
        logger.info("desktop acceptance script loaded: " + *path);
    }
#endif
    // Declared after the webview and AppState, so it is joined before either can be destroyed.
    detail::AsyncCommandRunner async_commands;
    NativeUiHooks native_ui{[] { Overlay::instance().dismiss(); }, {}};
    if (acceptance_script) {
        native_ui.report_acceptance_verdict = [data_dir, &w](const nlohmann::json& verdict) {
            write_acceptance_verdict(data_dir / "acceptance-verdict.json", verdict);
            // Let the binding resolve before ending the same run loop that delivered it.
            w.dispatch([&w] { w.terminate(); });
        };
    }
    register_commands(w, *state, data_dir, async_commands, capability_token,
                      std::move(native_ui));

    // Tray: click or "Show" raises the window, "Quit" ends the run loop. Closing the window
    // hides it to the tray. The native window handle is read once here, on the UI thread.
    //
    // Explain close-to-tray once (claim_tray_close_notice persists that it was shown).
    const auto explain_close_to_tray = [state = state.get(), &logger] {
        run_tray_action(logger, "close-to-tray notice", [state] {
            if (state->claim_tray_close_notice()) {
                Tray::instance().show_notification(build_close_to_tray_notification());
            }
        });
    };

    std::function<void()> raise_window;

    // One click handler for both native surfaces. Raising the window comes first and is
    // unconditional; then the claim decides whether the destination is still live (false means
    // already acted on or superseded). Returns whether an action ran, so the overlay knows
    // whether its dismiss callback still has work. `raise_window` is assigned below; both
    // outlive w.run().
    const auto route_alert_click = [&w, state = state.get(), &logger, &raise_window](
                                       AlertEvent event, std::int64_t alert_id) {
        if (raise_window) raise_window();
        if (!state->claim_alert_action(event, alert_id)) {
            logger.info(std::string("alert click: nothing to act on for ") +
                        std::to_string(alert_id) + " (already used, stale, or not actionable)");
            return false;
        }
        const auto action = alert_action_for(event);
        logger.info(std::string("alert click: ") + alert_action_as_str(action));
        switch (action) {
            case AlertAction::ReturnToWork:
                // Raises the recorded window and unlatches the tracker. Failures are logged
                // here, since nothing else on this path sees them.
                return run_tray_action(logger, "return to work", [state, &logger] {
                    const auto result = state->restore_snapback_target();
                    if (!result.ok) logger.warn("return to work: " + result.message);
                });
            case AlertAction::OpenSessionComposer:
            case AlertAction::OpenPomodoro:
                // The frontend owns surfaces; this side only names the destination.
                emit(w, events::kAlertAction,
                     dump_alert_action_event(action, alert_id));
                break;
            case AlertAction::None:
                return false;
        }
        return true;
    };

#if defined(_WIN32)
    if (auto win = w.window(); win.ok()) {
        HWND main_hwnd = reinterpret_cast<HWND>(win.value());
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
        callbacks.on_notification_click = route_alert_click;
        // Close-to-tray only once the icon is installed, or the window could vanish
        // unreachably.
        if (Tray::instance().install(std::move(callbacks))) enable_close_to_tray(main_hwnd, explain_close_to_tray);
    }
#elif defined(__APPLE__)
    // webview's window() hands back the NSWindow* as an opaque void*; mac_ui.mm is where
    // it becomes AppKit again, so main.cpp stays plain C++ (see mac_ui.hpp).
    if (auto win = w.window(); win.ok()) {
        void* main_window = win.value();
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
        callbacks.on_notification_click = route_alert_click;
        // Close-to-tray only once the icon is installed, or the window could vanish
        // unreachably.
        if (Tray::instance().install(std::move(callbacks))) enable_close_to_tray(main_window, explain_close_to_tray);
    }
#endif

    // The overlay card's action. The overlay has no alert id of its own and only shows the
    // newest card, so the outstanding id is read from state.
    Overlay::instance().set_action_callback([state = state.get(), &route_alert_click] {
        return route_alert_click(AlertEvent::Snapback,
                                 state->outstanding_alert_id(AlertEvent::Snapback));
    });

    // The owner's half of the activation channel. on_activate runs on the listener thread, so
    // it only dispatches to the UI thread.
    std::optional<ActivationListener> activation_listener;
    if (raise_window) {
        activation_listener = ActivationListener::start(data_dir, [&w, raise_window, &logger] {
            logger.info("activation: a second launch asked for the window");
            w.dispatch([raise_window] { raise_window(); });
        });
        if (!activation_listener) {
            // Not fatal, but logged.
            logger.warn("activation channel unavailable — a second launch cannot raise this "
                        "window and will report that it could not");
        } else {
            logger.info("activation channel listening on " + activation_listener->endpoint());
        }
    }

    // Engine events arrive off-thread; webview.eval and the overlay need the UI thread, so
    // dispatch. Copy event/payload by value.
    state->set_emit_hook([&w, state = state.get()](const char* event,
                                                   const std::string& payload,
                                                   AppState::ActivityEpoch activity_epoch) {
        std::string ev = event;
        w.dispatch([&w, state, ev, payload, activity_epoch] {
            // Reject stale ticks right before any user-visible side effect: a delete may have
            // run on the UI thread since this was queued.
            if (!state->activity_epoch_is_current(activity_epoch)) return;
            emit(w, ev.c_str(), payload);
            // Channels come from app/alert_routing.hpp on the payload; nothing is decided here.
            // The overlay is topmost and non-activating, so it reaches the user without a
            // toast.
            if (ev == events::kSnapback) {
                try {
                    const auto parsed = nlohmann::json::parse(payload);
                    const auto snap = parsed.get<SnapbackPayload>();
                    // A missing delivery block is a bug; fall back to defaults rather than
                    // going silent.
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
            // Hyperfocus defaults to a toast, not the overlay, to avoid interrupting deep work.
            if (ev == events::kHyperfocus) {
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

    // Log the URL: a malformed file URL and a missing bundle both render a blank window.
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

        async_commands.shutdown();
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
