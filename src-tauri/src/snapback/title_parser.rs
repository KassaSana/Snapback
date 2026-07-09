#[derive(Debug, Clone, Default)]
pub struct ParsedTitle {
    pub file_hint: String,
    pub project_hint: String,
    pub summary: String,
}

pub fn parse_window_title(app_name: &str, window_title: &str) -> ParsedTitle {
    let title = window_title.trim();
    if title.is_empty() {
        return ParsedTitle {
            file_hint: String::new(),
            project_hint: String::new(),
            summary: format!("Working in {app_name}"),
        };
    }

    let lower_app = app_name.to_lowercase();
    let parts: Vec<&str> = title.split(" - ").collect();

    if lower_app.contains("code") || lower_app.contains("cursor") {
        let file = parts.first().unwrap_or(&title).trim();
        // Only a genuine 3-part title ("file - project - App Name") carries a
        // project segment. With 1 or 2 parts there's no project open, so this
        // must resolve to "" — computing the index as `len - 2` without this
        // guard collapses to 0 (the same slot as `file`) when len == 2,
        // silently duplicating the filename into project_hint instead of
        // leaving it empty.
        let project = if parts.len() >= 3 {
            parts[parts.len() - 2].trim()
        } else {
            ""
        };
        return ParsedTitle {
            file_hint: file.to_string(),
            project_hint: project.to_string(),
            summary: if project.is_empty() {
                format!("Editing {file}")
            } else {
                format!("Editing {file} in {project}")
            },
        };
    }

    if parts.len() >= 2 {
        let head = parts[0].trim();
        ParsedTitle {
            file_hint: head.to_string(),
            project_hint: parts[1].trim().to_string(),
            summary: format!("{head} — {}", parts[1].trim()),
        }
    } else {
        ParsedTitle {
            file_hint: title.to_string(),
            project_hint: String::new(),
            summary: format!("{title} ({app_name})"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_vscode_title_with_project_segment() {
        let parsed = parse_window_title("Code", "auth.ts - snapback - Visual Studio Code");
        assert_eq!(parsed.file_hint, "auth.ts");
        assert_eq!(parsed.project_hint, "snapback");
        assert!(parsed.summary.contains("auth.ts"));
        assert!(parsed.summary.contains("snapback"));
    }

    #[test]
    fn parses_cursor_title_with_project_segment() {
        // "cursor" match is independent of "code" — verify it separately.
        let parsed = parse_window_title("Cursor", "main.rs - snapback - Cursor");
        assert_eq!(parsed.file_hint, "main.rs");
        assert_eq!(parsed.project_hint, "snapback");
    }

    #[test]
    fn editor_title_with_no_project_open_has_empty_project_hint() {
        // Regression test: no workspace/folder open means the title is just
        // "file - App Name" (2 segments). Before the fix, the project index
        // was computed as `len - 2`, which is 0 for len == 2 — the same slot
        // as `file` — so project_hint silently duplicated file_hint and the
        // summary read "Editing auth.ts in auth.ts".
        let parsed = parse_window_title("Code", "auth.ts - Visual Studio Code");
        assert_eq!(parsed.file_hint, "auth.ts");
        assert_eq!(parsed.project_hint, "");
        assert_eq!(parsed.summary, "Editing auth.ts");
    }

    #[test]
    fn editor_title_with_no_separator_has_empty_project_hint() {
        // Only 1 segment at all (no " - " anywhere in the title).
        let parsed = parse_window_title("Code", "Visual Studio Code");
        assert_eq!(parsed.file_hint, "Visual Studio Code");
        assert_eq!(parsed.project_hint, "");
        assert_eq!(parsed.summary, "Editing Visual Studio Code");
    }

    #[test]
    fn empty_title_falls_back_to_working_in_app_name() {
        let parsed = parse_window_title("Slack", "");
        assert_eq!(parsed.file_hint, "");
        assert_eq!(parsed.project_hint, "");
        assert_eq!(parsed.summary, "Working in Slack");

        // Whitespace-only titles are trimmed before the emptiness check.
        let whitespace_only = parse_window_title("Slack", "   ");
        assert_eq!(whitespace_only.summary, "Working in Slack");
    }

    #[test]
    fn non_editor_title_with_two_segments_uses_them_directly() {
        let parsed = parse_window_title("Google Chrome", "Inbox - Gmail");
        assert_eq!(parsed.file_hint, "Inbox");
        assert_eq!(parsed.project_hint, "Gmail");
        assert_eq!(parsed.summary, "Inbox — Gmail");
    }

    #[test]
    fn non_editor_title_with_one_segment_has_no_project_hint() {
        let parsed = parse_window_title("Terminal", "zsh");
        assert_eq!(parsed.file_hint, "zsh");
        assert_eq!(parsed.project_hint, "");
        assert_eq!(parsed.summary, "zsh (Terminal)");
    }
}
