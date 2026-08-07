// "Export my data in a legible form." Roadmap 7.6.
//
// Snapback already had two exports and neither answers the question a user is actually asking:
//
//  - `export_training_data` writes a 35-column feature matrix. It is for the training pipeline;
//    a person reading it learns nothing about their own day.
//  - `export_summary_report` writes aggregate JSON. It says how focused you were, not what was
//    recorded about you.
//
// This one is the personal archive: the sessions, their outcomes, and — the part that matters
// for a program that reads window titles — the captured windows themselves, in the order they
// happened. Markdown because it has to open in anything, on a machine that may no longer have
// Snapback installed.
//
// The renderer is pure and takes already-fetched rows: no database handle, no filesystem, no
// clock. That is what makes the escaping rules below testable, and they need to be, because
// every string it formats is untrusted — window titles are whatever the user's other programs
// put on screen.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "types.hpp"

namespace snapback {

struct PersonalArchiveSession {
    SessionRecord record;
    SessionRecap recap;
    std::vector<ContextSnapshotDto> context;
    // Roadmap 2.15. The interruptions recorded during this session. The recap has always
    // reported a *count* of these; a person asking what Snapback holds on them is owed the
    // episodes themselves, not a number they cannot check.
    std::vector<SnapbackEpisode> episodes;
    // True when more windows were captured than this archive lists. Stated in the output
    // rather than silently dropped: an export that quietly omits data is worse than one that
    // admits a limit, because the user believes they are holding everything.
    bool context_truncated = false;
};

struct PersonalArchive {
    std::string generated_at;
    std::string app_version;
    std::vector<PersonalArchiveSession> sessions;
    // True when older sessions exist beyond the export's session cap.
    bool sessions_truncated = false;
};

// What the IPC command reports back. The counts are what the UI shows, so the user can tell a
// successful export of an empty database ("0 sessions") from one that quietly wrote nothing.
struct PersonalArchiveExport {
    std::string output_path;
    std::size_t session_count = 0;
    std::size_t window_count = 0;
    bool truncated = false;
};

// Markdown-safe rendering of one untrusted cell. `|` would otherwise start a new column and a
// newline would end the row, so a window title like "a | b" silently corrupts every column
// after it — the sort of bug that looks like a formatting nit until you notice the export is
// no longer a faithful record.
std::string escape_table_cell(std::string_view value);

// The archive as a Markdown document. Always returns a complete document, including for an
// empty archive: a user who has recorded nothing should get a file that says so, not a
// zero-byte file that looks like the export failed.
std::string render_personal_archive(const PersonalArchive& archive);

}  // namespace snapback
