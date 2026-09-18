// The webview IPC bridge: binds whatever the CommandRegistry holds (Roadmap 14.3).
//
// We register each command with webview.bind(name, handler): the handler receives the call
// arguments as a JSON *array* string, run_json_command takes element [0] (the args object
// the shim forwarded), calls the registered handler, and returns JSON. The command names
// must match the frontend's invoke() calls -- that match is the IPC contract, pinned by
// test_ipc_contract against the registry itself rather than this file's text.
#pragma once

#include "app/webview_compat.hpp"  // webview.h + X11 macro scrub — never include webview.h raw

#include <filesystem>
#include <string>

#include "app/async_command_runner.hpp"
#include "app/command_dispatch.hpp"  // pure, webview-free dispatch + validation
#include "app/command_handlers.hpp"
#include "app/command_registry.hpp"
#include "app/state.hpp"
#include "snapback/overlay.hpp"

namespace snapback {
namespace detail {

// Roadmap 8.14. Every command, without exception -- including the read-only ones -- carries
// the capability token check. A page that can read your session history has read your
// window titles, so "only mutations need the check" would be the wrong boundary. Binding
// from the registry rather than one call site per command is what makes "every" a property
// of this loop instead of a discipline.
inline void bind_registry(webview::webview& w, const CommandRegistry& registry,
                          AsyncCommandRunner& async_commands, const std::string& token) {
    registry.for_each([&](const CommandDescriptor& command) {
        if (!command.async) {
            w.bind(command.name,
                   [handler = command.handler, token](const std::string& req) -> std::string {
                       return run_json_command(handler, req, token);
                   });
            return;
        }
        // Slow commands run on the owned worker so a slow disk cannot freeze the window,
        // one at a time per gate; webview's asynchronous overload returns to the UI loop
        // before the worker finishes and resolves the promise by id later.
        w.bind(
            command.name,
            [&w, &async_commands, handler = command.handler, token, policy = *command.async](
                std::string id, std::string req, void*) {
                dispatch_single_flight(
                    [&async_commands](std::function<void()> job) {
                        return async_commands.submit(std::move(job));
                    },
                    [&w, id](std::string result) { w.resolve(id, 0, std::move(result)); },
                    handler, std::move(req), token, policy.gate, policy.busy_message,
                    policy.on_claimed);
            },
            nullptr);
    });
}

}  // namespace detail

// Registers every command the frontend calls. `data_dir` is where training exports
// are written.
inline void register_commands(webview::webview& w, AppState& state,
                              const std::filesystem::path& data_dir,
                              detail::AsyncCommandRunner& async_commands,
                              const std::string& capability_token = {},
                              NativeUiHooks ui = {}) {
    CommandRegistry registry;
    if (!ui.dismiss_overlay) {
        ui.dismiss_overlay = [] { Overlay::instance().dismiss(); };
    }
    register_command_handlers(registry, state, data_dir, async_commands, std::move(ui));
    detail::bind_registry(w, registry, async_commands, capability_token);
}

// Push an event to the frontend. The shim listens on window.__snapback.emit. MUST run
// on the UI
// thread (see AppState's emit hook, which marshals via webview.dispatch).
inline void emit(webview::webview& w, const char* event, const std::string& json_payload) {
    w.eval(detail::event_dispatch_script(event, json_payload));
}

}  // namespace snapback
