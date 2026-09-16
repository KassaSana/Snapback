#include "app/command_registry.hpp"

#include <stdexcept>

namespace snapback {

namespace {

void insert_or_throw(std::map<std::string, CommandDescriptor>& commands,
                     CommandDescriptor descriptor) {
    const auto name = descriptor.name;
    if (!commands.emplace(name, std::move(descriptor)).second) {
        throw std::logic_error("command registered twice: " + name);
    }
}

}  // namespace

void CommandRegistry::add(const std::string& name, detail::JsonHandler handler) {
    insert_or_throw(commands_, CommandDescriptor{name, std::move(handler), std::nullopt});
}

void CommandRegistry::add_async(const std::string& name, detail::JsonHandler handler,
                                std::shared_ptr<std::atomic<bool>> gate,
                                std::string busy_message, std::function<void()> on_claimed) {
    insert_or_throw(commands_,
                    CommandDescriptor{name, std::move(handler),
                                      AsyncCommandPolicy{std::move(gate), std::move(busy_message),
                                                         std::move(on_claimed)}});
}

const CommandDescriptor* CommandRegistry::find(const std::string& name) const {
    const auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : &it->second;
}

std::vector<std::string> CommandRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(commands_.size());
    for (const auto& [name, descriptor] : commands_) out.push_back(name);
    return out;  // std::map iterates in key order
}

std::string CommandRegistry::invoke(const std::string& name, const std::string& request_array,
                                    const std::string& expected_token) const {
    const auto* descriptor = find(name);
    if (!descriptor) {
        const detail::JsonHandler unknown = [name](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("unknown command: " + name);
        };
        return detail::run_json_command(unknown, request_array, expected_token);
    }
    return detail::run_json_command(descriptor->handler, request_array, expected_token);
}

nlohmann::json CommandRegistry::call(const std::string& name, const nlohmann::json& args) const {
    const auto reply = nlohmann::json::parse(invoke(name, nlohmann::json::array({args}).dump()));
    if (reply.is_object() && reply.contains(detail::kErrorKey)) {
        throw std::runtime_error(reply.at(detail::kErrorKey).get<std::string>());
    }
    return reply;
}

}  // namespace snapback
