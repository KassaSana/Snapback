// Every native command, registered into a CommandRegistry (Roadmap 14.3). Webview-free:
// the same function builds the registry the app binds and the one the tests invoke.
#pragma once

#include <filesystem>
#include <functional>

#include "app/async_command_runner.hpp"
#include "app/command_registry.hpp"
#include "app/state.hpp"

namespace snapback {

// What the handlers need from the native UI, passed in rather than reached for: the
// overlay's implementation is Win32/AppKit and lives only in the app target, so the handler
// table would otherwise not link headless. Bind callbacks run on the UI thread, which is
// where the native card may be touched, so the app passes Overlay::instance().dismiss and
// a test passes something it can count.
struct NativeUiHooks {
    std::function<void()> dismiss_overlay;
};

// `data_dir` is where exports and the training log are written. `async_commands` is the
// worker the slow commands' policies refer to; its lifetime must cover the registry's use.
void register_command_handlers(CommandRegistry& registry, AppState& state,
                               const std::filesystem::path& data_dir,
                               detail::AsyncCommandRunner& async_commands,
                               NativeUiHooks ui = {});

}  // namespace snapback
