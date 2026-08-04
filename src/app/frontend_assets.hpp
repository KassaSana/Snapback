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

// ROADMAP 8.8. The webview's debug surface is developer tooling attached to a page that owns
// the full native command bridge, so it belongs behind the same Debug-only boundary as the
// override above — which is why it lives beside it rather than in `main.cpp`.
//
// Split in two deliberately. `webview_debug_for_build` is the rule, a pure function of the
// build kind; `kWebviewDebugEnabled` is this build's answer. A test binary is compiled in
// exactly one configuration, so asserting the constant alone could only ever cover the half
// the test happened to be built in — the release half would go unchecked in every local
// Debug run, which is the half that matters.
constexpr bool webview_debug_for_build(bool release_build) { return !release_build; }

#if defined(NDEBUG)
inline constexpr bool kReleaseBuild = true;
#else
inline constexpr bool kReleaseBuild = false;
#endif

inline constexpr bool kWebviewDebugEnabled = webview_debug_for_build(kReleaseBuild);

}  // namespace snapback
