// The native command set as data: every command the frontend can invoke, with its handler
// and, for the slow ones, the worker policy the bridge applies. Webview-free on purpose.
//
// Roadmap 14.3. Before this, the registered handlers existed only as closures handed
// straight to webview.bind() inside a function that could not be compiled without the
// webview headers. Tests could exercise the dispatcher and re-create individual handler
// lambdas by hand, and the IPC contract test matched command names by regex over the
// source text -- so a real handler's payload could drift with nothing to catch it, and a
// bug in the wiring of a real command (the training run on the synchronous binding, found
// 2026-09-16) was invisible to every test.
//
// Now the handlers register here, the webview adapter (app/commands.hpp) binds whatever is
// registered, and a test can build the same registry against a real AppState and invoke any
// command by name through the same envelope the bridge uses.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/command_dispatch.hpp"

namespace snapback {

// How a command that must not run on the webview's thread is dispatched: a single-flight
// gate per command family, the message when the gate is held, and an optional hook run on
// the dispatching thread once the gate is claimed (see dispatch_single_flight).
struct AsyncCommandPolicy {
    std::shared_ptr<std::atomic<bool>> gate;
    std::string busy_message;
    std::function<void()> on_claimed;
};

struct CommandDescriptor {
    std::string name;
    detail::JsonHandler handler;
    // Empty for a synchronous command, which the bridge runs inline on its own thread.
    std::optional<AsyncCommandPolicy> async;
};

class CommandRegistry {
public:
    // A name registered twice is a bug in the handler table, not a runtime condition, and
    // the contract fixture would not catch it (a set has no duplicates). Throws.
    void add(const std::string& name, detail::JsonHandler handler);
    void add_async(const std::string& name, detail::JsonHandler handler,
                   std::shared_ptr<std::atomic<bool>> gate, std::string busy_message,
                   std::function<void()> on_claimed = {});

    const CommandDescriptor* find(const std::string& name) const;
    std::vector<std::string> names() const;  // sorted
    std::size_t size() const { return commands_.size(); }

    // Iterate for binding. Descriptors are copied into the bridge's closures, so the
    // registry itself need not outlive the bind.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (const auto& [name, descriptor] : commands_) fn(descriptor);
    }

    // The test seam: run a command exactly as the bridge would frame it -- the request is
    // the JSON *array* the shim sends, the reply is the JSON string the shim receives,
    // errors arrive as the {__snapback_error} envelope -- but inline, on this thread. An
    // async command's handler runs the same way; its gate and worker are the adapter's
    // concern and are not simulated here. An unknown name is an error envelope too, which
    // is what a stale frontend call would see.
    std::string invoke(const std::string& name, const std::string& request_array,
                       const std::string& expected_token = {}) const;

    // Convenience over invoke(): pass the args object, get the parsed reply, and throw
    // std::runtime_error with the message on an error envelope.
    nlohmann::json call(const std::string& name,
                        const nlohmann::json& args = nlohmann::json::object()) const;

private:
    std::map<std::string, CommandDescriptor> commands_;
};

}  // namespace snapback
