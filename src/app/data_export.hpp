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
//
// Roadmap 9.16. `truncated` used to be a stored bool set from the *session* cap alone, so an
// archive that dropped the 501st window of an included session reported itself as complete.
// It is now derived from per-record-type omission counts, which makes that particular lie
// unrepresentable rather than merely fixed.
struct PersonalArchiveExport {
    std::string output_path;
    std::size_t session_count = 0;
    std::size_t window_count = 0;
    std::size_t episode_count = 0;
    // Zero everywhere now that the export is complete. The fields stay because a future cap
    // must not be reintroducible without saying which record type it applies to.
    std::size_t omitted_sessions = 0;
    std::size_t omitted_windows = 0;
    // A checksum of the document body, also written into the file's own footer. It exists so
    // a truncated or interrupted file is distinguishable from a valid empty one — the item's
    // requirement — not as tamper protection, which it is not.
    std::string checksum;

    [[nodiscard]] bool truncated() const {
        return omitted_sessions > 0 || omitted_windows > 0;
    }
};

// Roadmap 9.16. The archive is written incrementally rather than built in memory and returned
// as one string: a complete export of a long history is unbounded, and materializing it under
// `storage_mutex_` is the stall-becomes-dropped-events path 7.12 exists to avoid. These are
// the pieces, in the order they are emitted. `render_personal_archive` below composes them and
// remains the tested, non-streaming path for a whole in-memory archive.
std::string render_archive_header(const PersonalArchive& archive);
// The per-session heading and metadata, without its window rows.
std::string render_archive_session_header(const PersonalArchiveSession& session,
                                          std::size_t index_from_one);
// The interruptions table, or an empty string when there were none.
std::string render_archive_episodes(const std::vector<SnapbackEpisode>& episodes);
// The header row of the windows table. Emitted once per session that has any.
std::string render_archive_window_table_header();
// One window row. Called per row so a page of them never accumulates.
std::string render_archive_window_row(const ContextSnapshotDto& snapshot);
// Closes the document with the counts it actually wrote and a checksum of everything above.
std::string render_archive_footer(const PersonalArchiveExport& totals);

// FNV-1a over the bytes of the document body. Deliberately not a cryptographic hash: its job
// is to tell a truncated file from a complete one, which is what the item asks for, and
// claiming more than that in a filename would be worse than claiming nothing.
std::string archive_checksum(std::string_view body);

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
