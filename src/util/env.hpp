#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace snapback {

// Reads an environment variable. MSVC deprecates std::getenv, so Windows uses _dupenv_s.
inline std::optional<std::string> env_var(const char* name) {
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

}  // namespace snapback
