#include "app/data_export.hpp"

#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>

namespace snapback {
namespace {

std::string or_unknown(const std::optional<std::string>& value) {
    return value && !value->empty() ? *value : "unknown";
}

std::string or_placeholder(std::string_view value, std::string_view placeholder) {
    return value.empty() ? std::string(placeholder) : std::string(value);
}

// Minutes, because a duration in seconds is a number the reader has to convert. Rounded to
// nearest rather than truncated so a 119-second session does not read as "1 min".
std::string duration_minutes(std::uint64_t seconds) {
    return std::to_string((seconds + 30) / 60) + " min";
}

// Interruptions are usually under a minute, so minutes would round most of them to "0 min".
// Seconds below two minutes, minutes above — whichever the reader does not have to convert.
std::string duration_seconds(std::uint32_t seconds) {
    if (seconds < 120) return std::to_string(seconds) + " sec";
    return duration_minutes(seconds);
}

std::string one_decimal(double value) {
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << value;
    return out.str();
}

}  // namespace

std::string escape_table_cell(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '|':
                // Escaped, not dropped: the pipe is part of the title the user actually saw,
                // and an archive that edits its own contents is not a record.
                out += "\\|";
                break;
            case '\n':
            case '\r':
                // A row cannot span lines in a Markdown table. Rendered as a visible marker so
                // a multi-line title stays one row and still shows that a break was there.
                out += "⏎";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string archive_checksum(std::string_view body) {
    // FNV-1a 64. Small, dependency-free, and adequate for "did this file arrive whole".
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : body) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string render_archive_header(const PersonalArchive& archive) {
    std::ostringstream out;
    out << "# Your Snapback data\n\n";
    out << "Exported " << or_placeholder(archive.generated_at, "at an unknown time");
    if (!archive.app_version.empty()) out << " by Snapback " << archive.app_version;
    out << ".\n\n";

    // The same shape as the support bundle's privacy notice: say what is in the file *and*
    // what is not, so the user can tell whether this is the complete answer to "what do you
    // have on me" before they decide to send it to anyone.
    out << "**What this file contains:** every session Snapback recorded, its goal and times, "
           "how it scored, the interruptions it recorded, and the windows that were captured "
           "while it ran - including window titles, which can name files, people, and "
           "sites.\n\n";
    out << "**What it does not contain:** the raw keystroke and mouse timing rows behind the "
           "scores (use \"Export training data\" for those), and your settings.\n\n";
    out << "This file was written from the local database on this device. Nothing was uploaded "
           "to produce it, and nothing leaves this device unless you send this file "
           "somewhere.\n\n";
    out << "## Sessions\n\n";
    return out.str();
}

std::string render_archive_session_header(const PersonalArchiveSession& session,
                                          std::size_t index_from_one) {
    const auto& record = session.record;
    const auto& recap = session.recap;
    std::ostringstream out;
    out << "### " << index_from_one << ". "
        << or_placeholder(record.goal, "Untitled session") << "\n\n";
    out << "- Session id: `" << record.session_id << "`\n";
    out << "- Started: " << or_unknown(record.started_at) << "\n";
    out << "- Ended: " << or_unknown(record.ended_at) << "\n";
    out << "- Status: " << or_placeholder(record.status, "unknown") << "\n";
    out << "- Focus mode: " << or_placeholder(record.focus_mode, "unknown") << "\n";
    out << "- Duration: " << duration_minutes(recap.duration_secs) << "\n";
    out << "- Average focus score: " << one_decimal(recap.avg_focus_score) << " / 100\n";
    out << "- Deep focus: " << one_decimal(recap.deep_focus_pct) << "%\n";
    out << "- Interruptions recorded: " << recap.snapback_count << "\n\n";
    return out.str();
}

std::string render_archive_episodes(const std::vector<SnapbackEpisode>& episodes) {
    // Roadmap 2.15. Listed before the window table because it is the shorter, more
    // interesting list: an interruption log is a summary of the session, where the window
    // capture is the raw material.
    if (episodes.empty()) return {};
    std::ostringstream out;
    out << "#### Interruptions\n\n";
    out << "| Left at | Came back | Away for | Returned to |\n";
    out << "| --- | --- | --- | --- |\n";
    for (const auto& episode : episodes) {
        out << "| " << escape_table_cell(or_placeholder(episode.started_at, "unknown"))
            << " | " << escape_table_cell(or_placeholder(episode.ended_at, "unknown"))
            << " | " << duration_seconds(episode.duration_secs)
            << " | "
            << escape_table_cell(or_placeholder(
                   episode.file_hint.empty() ? episode.app_name : episode.file_hint, "unknown"))
            << " |\n";
    }
    out << "\n";
    return out.str();
}

std::string render_archive_window_table_header() {
    return "#### Windows captured\n\n| Time | App | Window title |\n| --- | --- | --- |\n";
}

std::string render_archive_window_row(const ContextSnapshotDto& snapshot) {
    std::ostringstream out;
    out << "| " << escape_table_cell(snapshot.timestamp)
        << " | " << escape_table_cell(snapshot.app_name)
        << " | " << escape_table_cell(snapshot.window_title) << " |\n";
    return out.str();
}

std::string render_archive_footer(const PersonalArchiveExport& totals) {
    // Roadmap 9.16. The manifest. Counts per record type, so "complete" is a claim the reader
    // can check against the tables above rather than take on trust, and a checksum so a file
    // that was cut short is distinguishable from one that legitimately holds nothing.
    std::ostringstream out;
    out << "\n---\n\n## What this export holds\n\n";
    out << "- Sessions: " << totals.session_count << "\n";
    out << "- Windows captured: " << totals.window_count << "\n";
    out << "- Interruptions: " << totals.episode_count << "\n";
    if (totals.truncated()) {
        out << "- **Omitted:** " << totals.omitted_sessions << " session(s) and "
            << totals.omitted_windows << " window(s) were left out of this file.\n";
    } else {
        out << "- Nothing was left out.\n";
    }
    out << "\nChecksum (FNV-1a, of everything above this section): `" << totals.checksum
        << "`. It tells a complete file from one that was cut short; it is not a signature.\n";
    out << "\n*End of export.*\n";
    return out.str();
}

std::string render_personal_archive(const PersonalArchive& archive) {
    // The non-streaming path: the same output as the streaming writer, composed from the same
    // pieces so the two cannot drift apart.
    std::ostringstream out;
    out << render_archive_header(archive);

    out << "Newest first. " << archive.sessions.size() << " session"
        << (archive.sessions.size() == 1 ? "" : "s") << " included";
    if (archive.sessions_truncated) {
        out << ", and older sessions exist beyond this export's limit";
    }
    out << ".\n\n";

    if (archive.sessions.empty()) {
        // A first-run export must still be a document. An empty file is indistinguishable
        // from a failed export, and this is the feature a privacy-minded user tries first.
        out << "No sessions have been recorded yet, so there is nothing here about you.\n";
        return out.str();
    }

    for (std::size_t i = 0; i < archive.sessions.size(); ++i) {
        const auto& session = archive.sessions[i];
        out << render_archive_session_header(session, i + 1);
        out << render_archive_episodes(session.episodes);

        if (session.context.empty()) {
            out << "No windows were captured during this session.\n\n";
            continue;
        }
        out << render_archive_window_table_header();
        for (const auto& snapshot : session.context) out << render_archive_window_row(snapshot);
        if (session.context_truncated) {
            out << "\nMore windows were captured during this session than are listed above.\n";
        }
        out << "\n";
    }

    return out.str();
}

}  // namespace snapback
