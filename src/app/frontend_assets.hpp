#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace snapback {

std::string file_url_from_path(const std::filesystem::path& path);
std::string resolve_frontend_url(const std::filesystem::path& exe_dir,
                                 const std::optional<std::string>& frontend_url_override,
                                 bool allow_override = true,
                                 bool allow_localhost_fallback = true);

// The webview debug surface is Debug-only. A pure rule plus this build's constant, so tests can
// check the Release half from a Debug build.
constexpr bool webview_debug_for_build(bool release_build) { return !release_build; }

#if defined(NDEBUG)
inline constexpr bool kReleaseBuild = true;
#else
inline constexpr bool kReleaseBuild = false;
#endif

inline constexpr bool kWebviewDebugEnabled = webview_debug_for_build(kReleaseBuild);

// ADR-0006: in-app train/deploy is developer tooling. Release keeps it off unless
// SNAPBACK_DEV_TRAINING is set.
constexpr bool developer_tools_for_build(bool release_build, bool env_override) {
    return !release_build || env_override;
}

bool developer_tools_enabled();

}  // namespace snapback
