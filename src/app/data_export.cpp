#include "app/data_export.hpp"

#include <cstdint>
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

std::string render_personal_archive(const PersonalArchive& archive) {
    std::ostringstream out;

    out << "# Your Snapback data\n\n";
    out << "Exported " << or_placeholder(archive.generated_at, "at an unknown time");
    if (!archive.app_version.empty()) out << " by Snapback " << archive.app_version;
    out << ".\n\n";

    // The same shape as the support bundle's privacy notice: say what is in the file *and*
    // what is not, so the user can tell whether this is the complete answer to "what do you
    // have on me" before they decide to send it to anyone.
    out << "**What this file contains:** every session Snapback recorded, its goal and times, "
           "how it scored, and the windows that were captured while it ran — including window "
           "titles, which can name files, people, and sites.\n\n";
    out << "**What it does not contain:** the raw keystroke and mouse timing rows behind the "
           "scores (use \"Export training data\" for those), and your settings.\n\n";
    out << "This file was written from the local database on this device. Nothing was uploaded "
           "to produce it, and nothing leaves this device unless you send this file "
           "somewhere.\n\n";

    if (archive.sessions.empty()) {
        // A first-run export must still be a document. An empty file is indistinguishable
        // from a failed export, and this is the feature a privacy-minded user tries first.
        out << "## Sessions\n\nNo sessions have been recorded yet, so there is nothing here "
               "about you.\n";
        return out.str();
    }

    out << "## Sessions\n\n";
    out << "Newest first. " << archive.sessions.size() << " session"
        << (archive.sessions.size() == 1 ? "" : "s") << " included";
    if (archive.sessions_truncated) {
        out << ", and older sessions exist beyond this export's limit";
    }
    out << ".\n\n";

    for (std::size_t i = 0; i < archive.sessions.size(); ++i) {
        const auto& session = archive.sessions[i];
        const auto& record = session.record;
        const auto& recap = session.recap;

        out << "### " << (i + 1) << ". "
            << or_placeholder(record.goal, "Untitled session") << "\n\n";
        out << "- Session id: `" << record.session_id << "`\n";
        out << "- Started: " << or_unknown(record.started_at) << "\n";
        out << "- Ended: " << or_unknown(record.ended_at) << "\n";
        out << "- Status: " << or_placeholder(record.status, "unknown") << "\n";
        out << "- Focus mode: " << or_placeholder(record.focus_mode, "unknown") << "\n";
        out << "- Duration: " << duration_minutes(recap.duration_secs) << "\n";
        out << "- Average focus score: " << one_decimal(recap.avg_focus_score) << " / 100\n";
        out << "- Deep focus: " << one_decimal(recap.deep_focus_pct) << "%\n";
        out << "- Snapback nudges: " << recap.snapback_count << "\n\n";

        // Roadmap 2.15. Listed before the window table because it is the shorter, more
        // interesting list: an interruption log is a summary of the session, where the window
        // capture is the raw material.
        if (!session.episodes.empty()) {
            out << "#### Interruptions\n\n";
            out << "| Left at | Came back | Away for | Returned to |\n";
            out << "| --- | --- | --- | --- |\n";
            for (const auto& episode : session.episodes) {
                out << "| " << escape_table_cell(or_placeholder(episode.started_at, "unknown"))
                    << " | " << escape_table_cell(or_placeholder(episode.ended_at, "unknown"))
                    << " | " << duration_seconds(episode.duration_secs)  //
                    << " | "
                    << escape_table_cell(or_placeholder(
                           episode.file_hint.empty() ? episode.app_name : episode.file_hint,
                           "unknown"))
                    << " |\n";
            }
            out << "\n";
        }

        if (session.context.empty()) {
            out << "No windows were captured during this session.\n\n";
            continue;
        }

        out << "#### Windows captured\n\n";
        out << "| Time | App | Window title |\n";
        out << "| --- | --- | --- |\n";
        for (const auto& snapshot : session.context) {
            out << "| " << escape_table_cell(snapshot.timestamp)  //
                << " | " << escape_table_cell(snapshot.app_name)  //
                << " | " << escape_table_cell(snapshot.window_title) << " |\n";
        }
        if (session.context_truncated) {
            out << "\nMore windows were captured during this session than are listed above.\n";
        }
        out << "\n";
    }

    return out.str();
}

}  // namespace snapback
