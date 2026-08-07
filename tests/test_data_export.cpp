#include "doctest_wrapper.hpp"

#include <string>

#include "app/data_export.hpp"

using namespace snapback;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

PersonalArchiveSession make_session(const std::string& id, const std::string& goal) {
    PersonalArchiveSession session;
    session.record.session_id = id;
    session.record.goal = goal;
    session.record.status = "COMPLETED";
    session.record.focus_mode = "normal";
    session.record.started_at = "2026-07-30T09:00:00Z";
    session.record.ended_at = "2026-07-30T09:45:00Z";
    session.recap.session_id = id;
    session.recap.goal = goal;
    session.recap.duration_secs = 2700;
    // Deliberately not a .x5 value: 71.25 is exactly representable, so printing it to one
    // decimal exercises IEEE round-half-to-even rather than the formatting this test is about.
    session.recap.avg_focus_score = 71.26;
    session.recap.deep_focus_pct = 40.0;
    session.recap.snapback_count = 2;
    return session;
}

ContextSnapshotDto make_window(const std::string& app, const std::string& title) {
    ContextSnapshotDto snapshot;
    snapshot.app_name = app;
    snapshot.window_title = title;
    snapshot.timestamp = "2026-07-30T09:05:00Z";
    return snapshot;
}

SnapbackEpisode make_episode(const std::string& id, std::uint32_t duration_secs,
                             const std::string& app, const std::string& file_hint) {
    SnapbackEpisode episode;
    episode.session_id = id;
    episode.summary = file_hint.empty() ? "Return to " + app : "Return to " + file_hint;
    episode.app_name = app;
    episode.file_hint = file_hint;
    episode.started_at = "2026-07-30T09:10:00Z";
    episode.ended_at = "2026-07-30T09:12:00Z";
    episode.duration_secs = duration_secs;
    return episode;
}

}  // namespace

// Roadmap 2.15. The recap has always reported an interruption count; until the episodes were
// stored, that number was both always zero and — had it not been — unverifiable by the person
// it describes. An export that states a count it cannot show is not an answer to "what do you
// have on me".
TEST_CASE("render_personal_archive lists the interruptions behind the count") {
    PersonalArchive archive;
    archive.generated_at = "2026-08-06T10:00:00Z";
    auto session = make_session("s1", "ship the export");
    session.episodes.push_back(make_episode("s1", 95, "Cursor", "state.cpp"));
    session.episodes.push_back(make_episode("s1", 240, "Cursor", ""));
    archive.sessions.push_back(std::move(session));

    const auto markdown = render_personal_archive(archive);
    CHECK(markdown.find("#### Interruptions") != std::string::npos);
    CHECK(markdown.find("| Left at | Came back | Away for | Returned to |") != std::string::npos);
    CHECK(markdown.find("2026-07-30T09:10:00Z") != std::string::npos);
    // Under two minutes reads in seconds; above it, minutes. Rounding 95 seconds to "2 min"
    // would make a brief glance at a notification look like a coffee break.
    CHECK(markdown.find("95 sec") != std::string::npos);
    CHECK(markdown.find("4 min") != std::string::npos);
    // The route back names the file when there is one and the app when there is not.
    CHECK(markdown.find("| state.cpp |") != std::string::npos);
    CHECK(markdown.find("| Cursor |") != std::string::npos);
}

TEST_CASE("render_personal_archive omits the interruptions table when there were none") {
    // An empty table with a heading reads as a missing section rather than a quiet session.
    PersonalArchive archive;
    archive.generated_at = "2026-08-06T10:00:00Z";
    archive.sessions.push_back(make_session("s1", "uninterrupted"));

    CHECK(render_personal_archive(archive).find("#### Interruptions") == std::string::npos);
}

TEST_CASE("render_personal_archive escapes an interruption's untrusted text") {
    // The file hint comes from a window title, so it is the same untrusted input the window
    // table escapes — a fact easy to forget when adding a second table.
    PersonalArchive archive;
    archive.generated_at = "2026-08-06T10:00:00Z";
    auto session = make_session("s1", "escaping");
    session.episodes.push_back(make_episode("s1", 60, "Cursor", "a | b"));
    archive.sessions.push_back(std::move(session));

    const auto markdown = render_personal_archive(archive);
    CHECK(markdown.find("a \\| b") != std::string::npos);
}

// A window title is whatever some other program put on screen, so it is untrusted input to the
// formatter. An unescaped `|` shifts every later column, which turns a record of what happened
// into a record of something else — quietly, and only in rows with unusual titles.
TEST_CASE("escape_table_cell keeps a pipe from opening a new column") {
    CHECK(escape_table_cell("budget | Q3 | draft") == "budget \\| Q3 \\| draft");
}

TEST_CASE("escape_table_cell keeps a newline from ending the row") {
    CHECK(escape_table_cell("line one\nline two") == "line one⏎line two");
    CHECK(escape_table_cell("crlf\r\nhere") == "crlf⏎⏎here");
}

TEST_CASE("escape_table_cell leaves ordinary titles untouched") {
    CHECK(escape_table_cell("main.cpp — Snapback") == "main.cpp — Snapback");
    CHECK(escape_table_cell("").empty());
}

// An empty export must still be a document. A zero-byte file is indistinguishable from a
// failed write, and this is the first thing a privacy-minded user clicks.
TEST_CASE("render_personal_archive documents an empty history instead of writing nothing") {
    PersonalArchive archive;
    archive.generated_at = "2026-07-30T10:00:00Z";
    archive.app_version = "0.2.0";

    const auto markdown = render_personal_archive(archive);

    CHECK(contains(markdown, "# Your Snapback data"));
    CHECK(contains(markdown, "2026-07-30T10:00:00Z"));
    CHECK(contains(markdown, "No sessions have been recorded yet"));
}

TEST_CASE("render_personal_archive reports each session and the windows it captured") {
    PersonalArchive archive;
    archive.generated_at = "2026-07-30T10:00:00Z";
    archive.app_version = "0.2.0";
    auto session = make_session("s-1", "refactor the parser");
    session.context.push_back(make_window("Code", "title_parser.cpp"));
    session.context.push_back(make_window("Safari", "cppreference"));
    archive.sessions.push_back(std::move(session));

    const auto markdown = render_personal_archive(archive);

    CHECK(contains(markdown, "refactor the parser"));
    CHECK(contains(markdown, "`s-1`"));
    CHECK(contains(markdown, "2026-07-30T09:00:00Z"));
    CHECK(contains(markdown, "45 min"));
    CHECK(contains(markdown, "71.3 / 100"));
    CHECK(contains(markdown, "| Time | App | Window title |"));
    CHECK(contains(markdown, "| Code | title_parser.cpp |"));
    CHECK(contains(markdown, "| Safari | cppreference |"));
    // The file has to state that it holds window titles; a user deciding whether to send it
    // somewhere needs to know that before they read it.
    CHECK(contains(markdown, "window titles"));
}

TEST_CASE("render_personal_archive escapes titles as it writes the table") {
    PersonalArchive archive;
    auto session = make_session("s-1", "work");
    session.context.push_back(make_window("Mail", "Re: budget | urgent"));
    archive.sessions.push_back(std::move(session));

    const auto markdown = render_personal_archive(archive);

    CHECK(contains(markdown, "Re: budget \\| urgent"));
}

// Truncation has to be visible. An archive that silently stops is worse than one that admits a
// limit, because the user believes they are holding the complete record.
TEST_CASE("render_personal_archive says so when it left data out") {
    PersonalArchive archive;
    archive.sessions_truncated = true;
    auto session = make_session("s-1", "work");
    session.context.push_back(make_window("Code", "main.cpp"));
    session.context_truncated = true;
    archive.sessions.push_back(std::move(session));

    const auto markdown = render_personal_archive(archive);

    CHECK(contains(markdown, "older sessions exist beyond this export's limit"));
    CHECK(contains(markdown, "More windows were captured during this session"));
}

TEST_CASE("render_personal_archive stays complete when a session recorded nothing") {
    PersonalArchive archive;
    archive.sessions.push_back(make_session("s-1", "planning"));

    const auto markdown = render_personal_archive(archive);

    CHECK(contains(markdown, "No windows were captured during this session."));
    CHECK_FALSE(contains(markdown, "| Time | App | Window title |"));
}

TEST_CASE("render_personal_archive names an untitled session rather than leaving a blank") {
    PersonalArchive archive;
    archive.sessions.push_back(make_session("s-1", ""));

    CHECK(contains(render_personal_archive(archive), "Untitled session"));
}
