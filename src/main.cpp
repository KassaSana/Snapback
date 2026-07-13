// Entry point. Rust: main.rs (calls run_from_cli) + lib.rs::run().
//
// Boot order mirrors the Rust setup() closure:
//   1. resolve app data dir            (Tauri: app.path().app_data_dir())
//   2. optionally load the ONNX model  (Rust: onnx_model::init behind a feature)
//   3. open SQLite storage             (Rust: Storage::open)
//   4. build AppState + start engine   (Rust: AppState::new + start_engine)
//   5. create the webview, bind IPC, load the React build, run the loop
#include <webview/webview.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "app/commands.hpp"
#include "app/frontend_assets.hpp"
#include "app/ipc_shim.hpp"
#include "app/state.hpp"
#include "app/tray.hpp"
#include "engine/onnx_model.hpp"
#include "snapback/overlay.hpp"
#include "storage/storage.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

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
#endif
    return std::filesystem::current_path();
}

}  // namespace

int main() {
    using namespace snapback;

    const auto data_dir = app_data_dir();
    std::filesystem::create_directories(data_dir);

#if defined(SNAPBACK_ONNX)
    if (auto model = OnnxModel::resolve_model_path(data_dir)) {
        OnnxModel::instance().init(*model);
    }
#endif

    auto storage = Storage::open(data_dir);
    if (!storage) {
        // Rust logs + exit(1) here so a headless failure isn't silent on Windows.
        return 1;
    }

    // Heap-allocate AppState: it embeds the 64K-slot capture ring buffer inline (~5 MB),
    // which blows the default 1 MB stack if placed as a local. The tests do the same via
    // make_unique. A unique_ptr keeps ownership + lifetime clear.
    auto state = std::make_unique<AppState>(std::move(*storage));
    state->start_engine();

    webview::webview w(/*debug=*/true, nullptr);
    w.set_title("Snapback");
    w.set_size(1100, 760, WEBVIEW_HINT_NONE);

    // Inject the Tauri-v2 IPC shim BEFORE any page script runs (init scripts run on
    // every navigation, ahead of the bundle), then register the command binds it calls.
    w.init(kIpcShim);
    register_commands(w, *state, data_dir);

    // System tray (Phase 8): left-click/double-click or the "Show" menu item brings the
    // window forward; "Quit" ends the run loop. Tauri gave us this for free; here it's
    // Shell_NotifyIcon behind the Tray interface.
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
    state->set_emit_hook([&w](const char* event, const std::string& payload) {
        std::string ev = event;
        w.dispatch([&w, ev, payload] {
            emit(w, ev.c_str(), payload);
            // On the return-from-distraction edge, also pop the native overlay card.
            if (ev == "snapback") {
                try {
                    Overlay::instance().show(nlohmann::json::parse(payload).get<SnapbackPayload>());
                } catch (...) {
                    // A malformed payload must never take down the UI thread.
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

    // Dev override: SNAPBACK_FRONTEND_URL=http://127.0.0.1:5173. Demo/release path:
    // load the bundled frontend/index.html copied next to snapback.exe.
    w.navigate(resolve_frontend_url(executable_dir(), env_var("SNAPBACK_FRONTEND_URL")));

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

    state->set_emit_hook(nullptr);  // stop emitting into a torn-down webview
    state->stop_engine();
    return 0;
}
