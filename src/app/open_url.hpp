// Open an http/https/mailto URL in the system browser. Roadmap 8.14.
//
// The privileged webview must not navigate its main frame to remote content. External links
// therefore leave through the OS opener, which has no native bridge. The URL is data, never
// program text — same rule as reveal_path.hpp.
#pragma once

#include <string>
#include <vector>

namespace snapback {

bool open_external_url_supported();

// Opens `url` when classify_navigation would hand it to the system browser. Returns false
// for blocked schemes, missing backends, or OS refusal — never throws across IPC.
bool open_external_url(const std::string& url);

// Pure, compiled on every OS so the argv contract is testable off Linux.
inline std::vector<std::string> external_url_argv(const std::string& url) {
    return {"xdg-open", url};
}

namespace detail {

bool open_allowed_external_url(const std::string& url);
bool open_external_url_impl(const std::string& url);

}  // namespace detail

}  // namespace snapback
