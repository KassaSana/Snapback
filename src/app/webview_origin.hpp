// Which document may drive the native command bridge. The shim runs on every navigation and
// every command is bound globally, so a redirected page would otherwise inherit the whole
// privileged surface. CSP does not answer this: it limits what a trusted document loads, not
// which document owns the bindings. Pure functions, testable without a webview.

#pragma once

#include <string>

namespace snapback {

// What to do with a top-level navigation the privileged webview is asked to perform.
enum class NavigationDecision {
    // The trusted packaged document (or, in a Debug build, the dev server).
    Allow,
    // A legitimate destination that is not ours — hand it to the system browser, which gets
    // no native bridge because it is a different program entirely.
    OpenExternally,
    // Neither: a scheme that has no business being a main frame here.
    Block,
};

// Canonicalizes a document URL: lowercase scheme, no query or fragment, `.`/`..` collapsed in
// file: paths (so a differently spelled path cannot bypass the comparison).
std::string canonical_document_url(const std::string& url);

// True when `url` is the one document allowed to invoke native commands. `debug_build` also
// admits loopback (the Vite dev server); release admits only the packaged file.
bool is_trusted_document(const std::string& url, const std::string& trusted_url,
                         bool debug_build);

// What should happen if the webview is asked to navigate its main frame to `target`.
NavigationDecision classify_navigation(const std::string& target,
                                       const std::string& trusted_url, bool debug_build);

// A per-launch capability token. The shim checks its own document before page scripts run and
// hands the token over only if trusted; every command requires it. Random per launch, so it
// cannot be read out of the binary.
std::string generate_capability_token();

}  // namespace snapback
