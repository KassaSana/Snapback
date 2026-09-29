// The personal archive: sessions, outcomes, and the captured windows, as Markdown so it opens
// anywhere. The renderer is pure over already-fetched rows. Every string is untrusted (window
// titles), hence the escaping below.
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
    // The interruptions recorded during this session.
    std::vector<SnapbackEpisode> episodes;
    // True when more windows were captured than this archive lists. Stated in the output
    // rather than silently dropped: an export that quietly omits data is worse than one that
    // admits a limit, because the user believes they are holding everything.
    bool context_truncated = false;
};

struct PersonalArchive {
    std::int64_t generated_at_ms{};
    std::string app_version;
    std::vector<PersonalArchiveSession> sessions;
    // True when older sessions exist beyond the export's session cap.
    bool sessions_truncated = false;
};

// What the IPC command reports. `truncated` is derived from per-type omission counts.
struct PersonalArchiveExport {
    std::string output_path;
    std::size_t session_count = 0;
    std::size_t window_count = 0;
    std::size_t episode_count = 0;
    // Zero everywhere now that the export is complete. The fields stay because a future cap
    // must not be reintroducible without saying which record type it applies to.
    std::size_t omitted_sessions = 0;
    std::size_t omitted_windows = 0;
    std::size_t omitted_episodes = 0;
    // Checksum of the body, also written in the footer, so a truncated file is distinguishable
    // from a valid empty one. Not tamper protection.
    std::string checksum;

    [[nodiscard]] bool truncated() const {
        return omitted_sessions > 0 || omitted_windows > 0 || omitted_episodes > 0;
    }
};

// The archive is written incrementally (a full history is unbounded); these are the pieces in
// emission order. render_personal_archive composes them for an in-memory archive.
std::string render_archive_header(const PersonalArchive& archive);
// The per-session heading and metadata, without its window rows.
std::string render_archive_session_header(const PersonalArchiveSession& session,
                                          std::size_t index_from_one);
// The interruptions table, or an empty string when there were none.
std::string render_archive_episodes(const std::vector<SnapbackEpisode>& episodes);
std::string render_archive_episode_table_header();
std::string render_archive_episode_row(const SnapbackEpisode& episode);
// The header row of the windows table. Emitted once per session that has any.
std::string render_archive_window_table_header();
// One window row. Called per row so a page of them never accumulates.
std::string render_archive_window_row(const ContextSnapshotDto& snapshot);
// Closes the document with the counts it actually wrote and a checksum of everything above.
std::string render_archive_footer(const PersonalArchiveExport& totals);

// FNV-1a over the body bytes; detects truncation, not tampering.
std::string archive_checksum(std::string_view body);

// Markdown-safe rendering of one untrusted cell: `|` and newlines would break the table.
std::string escape_table_cell(std::string_view value);

// The archive as a Markdown document. Always returns a complete document, including for an
// empty archive: a user who has recorded nothing should get a file that says so, not a
// zero-byte file that looks like the export failed.
std::string render_personal_archive(const PersonalArchive& archive);

}  // namespace snapback
