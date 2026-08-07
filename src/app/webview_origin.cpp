#include "app/webview_origin.hpp"

#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>
#include <vector>

namespace snapback {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string scheme_of(const std::string& url) {
    const auto colon = url.find(':');
    if (colon == std::string::npos) return {};
    return lower(url.substr(0, colon));
}

// Collapses `.` and `..` segments. A path alias is the cheapest way past a string comparison,
// and `file:` URLs are full of opportunities for one.
std::string normalize_path_segments(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream in(path);
    std::string segment;
    const bool absolute = !path.empty() && path.front() == '/';
    while (std::getline(in, segment, '/')) {
        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(segment);
    }
    std::string out;
    for (const auto& part : parts) {
        out += '/';
        out += part;
    }
    if (out.empty()) return absolute ? "/" : "";
    return out;
}

}  // namespace

std::string canonical_document_url(const std::string& url) {
    std::string value = url;

    // Query and fragment do not change which document is loaded, and leaving them in would
    // let `index.html?x=1` read as a different — untrusted — page than `index.html`.
    const auto cut = value.find_first_of("?#");
    if (cut != std::string::npos) value.erase(cut);

    const auto scheme = scheme_of(value);
    if (scheme.empty()) return lower(value);

    const auto after_scheme = value.substr(scheme.size() + 1);
    if (scheme == "file") {
        // Authority is always empty for our own file URLs; whatever slashes precede the path,
        // the path is what identifies the document.
        std::size_t start = 0;
        while (start < after_scheme.size() && after_scheme[start] == '/') ++start;
        std::string path = "/" + after_scheme.substr(start);
        // Windows paths are case-insensitive and may arrive with either separator. Lowercasing
        // is correct there and harmless for our own generated URLs elsewhere, which are
        // produced from a single code path (`file_url_from_path`).
        std::replace(path.begin(), path.end(), '\\', '/');
        return "file://" + lower(normalize_path_segments(path));
    }

    // http/https and everything else: scheme lowercased, authority lowercased, path collapsed.
    std::size_t start = 0;
    while (start < after_scheme.size() && after_scheme[start] == '/') ++start;
    const std::string rest = after_scheme.substr(start);
    const auto slash = rest.find('/');
    const std::string authority = lower(slash == std::string::npos ? rest : rest.substr(0, slash));
    const std::string path = slash == std::string::npos ? "" : rest.substr(slash);
    return scheme + "://" + authority + normalize_path_segments(path);
}

namespace {

bool is_loopback_authority(const std::string& canonical) {
    // Only the loopback names, and only over http. A hostname that merely *contains*
    // "localhost" (`localhost.evil.com`) is a different host entirely, which is why this
    // compares the authority rather than searching the URL.
    if (canonical.rfind("http://", 0) != 0) return false;
    const std::string rest = canonical.substr(std::string("http://").size());
    const auto slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    const auto colon = authority.find(':');
    if (colon != std::string::npos) authority.erase(colon);
    return authority == "localhost" || authority == "127.0.0.1" || authority == "[::1]";
}

}  // namespace

bool is_trusted_document(const std::string& url, const std::string& trusted_url,
                         bool debug_build) {
    if (url.empty() || trusted_url.empty()) return false;
    const auto canonical = canonical_document_url(url);
    if (canonical == canonical_document_url(trusted_url)) return true;
    // 8.8's build-time gate, reused: the dev server is a development affordance and must not
    // exist in a shipped build.
    return debug_build && is_loopback_authority(canonical);
}

NavigationDecision classify_navigation(const std::string& target, const std::string& trusted_url,
                                       bool debug_build) {
    if (target.empty()) return NavigationDecision::Block;
    if (is_trusted_document(target, trusted_url, debug_build)) return NavigationDecision::Allow;

    const auto scheme = scheme_of(canonical_document_url(target));
    if (scheme == "http" || scheme == "https" || scheme == "mailto") {
        // A real destination, just not one that may hold the native bridge. Handing it to the
        // system browser is the honest resolution: the user still gets where they were going,
        // in a program that has no access to their data.
        return NavigationDecision::OpenExternally;
    }

    // `file:` outside the bundle, plus `data:`, `javascript:`, `blob:`, `about:` and anything
    // unrecognized. None of these is a destination a user asked for, and each is a way to get
    // script running in the privileged frame. Blocked rather than externalized: handing
    // `javascript:` or a local `file:` to the system browser would be a different bug.
    return NavigationDecision::Block;
}

std::string generate_capability_token() {
    // 256 bits from the platform CSPRNG. Not a session identifier and not stored: it exists
    // for one process lifetime and never leaves it except into the trusted document.
    std::random_device device;
    std::uniform_int_distribution<unsigned> nibble(0, 15);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 64; ++i) token.push_back(kHex[nibble(device)]);
    return token;
}

}  // namespace snapback
