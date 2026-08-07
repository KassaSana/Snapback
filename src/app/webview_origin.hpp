// Which document is allowed to drive the native command bridge. Roadmap 8.14.
//
// **8.4 locks down the URL the app navigates to. 8.3 constrains what the bundled page may
// load. Neither survives a later top-level navigation.** `kIpcShim` is installed with
// `webview.init()`, whose own comment says it runs before page scripts on *every* navigation,
// and every command is bound onto the global object. A page reached by a redirect, or by a
// Help link somebody adds next year, therefore inherits `delete_all_activity_data`,
// `export_my_data`, `set_privacy_exclusions`, and the training commands — the entire
// privileged surface, granted to whatever happens to be loaded.
//
// **CSP is not the check.** It governs what resources a trusted document may pull in; it says
// nothing about which document owns the native bindings. The item is explicit about not
// confusing the two, and it would be an easy confusion to make, because 8.3 already added a
// CSP and it looks like it is about the same thing.
//
// The rules live here as pure functions so they can be tested without a webview, and so the
// answer to "may this document call native code" is one function rather than a habit.

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

// Canonicalizes a document URL for comparison.
//
// Lowercases the scheme, drops query and fragment, and collapses `.`/`..` inside a `file:`
// path. That last part is the one that matters: `file:///app/frontend/../frontend/index.html`
// and `file:///app/frontend/index.html` are the same document, and a comparison that misses
// it is a comparison an attacker only has to spell differently to defeat.
std::string canonical_document_url(const std::string& url);

// True when `url` is the one document allowed to invoke native commands.
//
// `debug_build` additionally admits loopback, and *only* loopback: the dev server is how the
// app is developed against Vite, and 8.8 already established that build-time gate for the
// webview's debug surface. A release build admits nothing but the packaged file.
bool is_trusted_document(const std::string& url, const std::string& trusted_url,
                         bool debug_build);

// What should happen if the webview is asked to navigate its main frame to `target`.
NavigationDecision classify_navigation(const std::string& target,
                                       const std::string& trusted_url, bool debug_build);

// A per-launch capability token.
//
// The enforcement mechanism, given that the webview facade exposes no navigation-intercept
// hook. The shim runs before page scripts on every navigation, checks its own document
// against the trusted URL, and hands the token to the page's bridge *only* if it matches.
// Every native command requires the token back. An untrusted document therefore has the bound
// functions on its global object and no way to call them successfully — it never sees the
// token, because the shim decided before the page ran a single line.
//
// Random per launch rather than fixed: a constant in the binary is a constant an attacker can
// read out of the binary.
std::string generate_capability_token();

}  // namespace snapback
