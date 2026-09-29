// The JavaScript IPC shim injected before the app bundle loads: a typed invoke/listen surface
// and event bus over webview.bind(). It runs before page scripts on every navigation and hands
// out the capability token only to the trusted packaged document.
#pragma once

#include <string>

namespace snapback {

// Builds the init script for webview.init(). `trusted_canonical_url` must already be
// canonicalized with canonical_document_url(); `capability_token` is the per-launch secret
// every native bind checks via run_json_command().
std::string build_ipc_shim_script(const std::string& trusted_canonical_url,
                                  const std::string& capability_token, bool debug_build);

}  // namespace snapback
