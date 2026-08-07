// The JavaScript IPC shim injected into the webview before the app bundle loads.
// webview.bind() exposes native commands, while this bridge gives the frontend a
// typed invoke/listen surface and a host-to-frontend event bus.
//
// Roadmap 8.14. The shim also decides which document may call those commands: it runs on
// every navigation before page scripts, checks the loaded URL against the one trusted
// packaged document, and attaches the per-launch capability token only when they match.
#pragma once

#include <string>

namespace snapback {

// Builds the init script for webview.init(). `trusted_canonical_url` must already be
// canonicalized with canonical_document_url(); `capability_token` is the per-launch secret
// every native bind checks via run_json_command().
std::string build_ipc_shim_script(const std::string& trusted_canonical_url,
                                  const std::string& capability_token, bool debug_build);

}  // namespace snapback
