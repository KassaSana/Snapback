// Extract file/project hints from a window title. Rust: snapback/title_parser.rs.
// Pure string parsing, so it ports 1:1 — and it's the one piece with real logic in
// this sketch, to show the porting style.
#pragma once

#include <string>

namespace snapback {

struct TitleHints {
    std::string file_hint;     // e.g. "auth.ts"
    std::string project_hint;  // e.g. "Snapback"
};

// Examples the Rust parser handles:
//   "auth.ts — Snapback — Visual Studio Code"  -> {auth.ts, Snapback}
//   "index.html - Chromium"                    -> {index.html, ""}
TitleHints parse_title(const std::string& window_title);

}  // namespace snapback
