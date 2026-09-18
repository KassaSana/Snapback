// Test-only support for Roadmap 10.1's real-webview acceptance round trip.
#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace snapback {

// Reads the JavaScript injected into an acceptance-enabled desktop build. A missing or empty
// script is a failed smoke setup, not a reason to launch an app the smoke cannot exercise.
std::string load_acceptance_script(const std::filesystem::path& path);

// Publishes the page-side verdict through a staged replacement so a smoke runner never
// mistakes a partial JSON write for a completed bridge round trip.
void write_acceptance_verdict(const std::filesystem::path& path,
                              const nlohmann::json& verdict);

}  // namespace snapback
