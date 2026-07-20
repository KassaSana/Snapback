// Structured leveled logger. Roadmap 4.1: levels + formatting + filtering + file rotation.
//
// Replaces scattered std::cerr writes with one leveled sink: every line is
// "<timestamp> [LEVEL] message", and anything below the configured level is dropped.
// RotatingFileStream adds a bounded on-disk sink; an in-app diagnostics view remains a
// follow-up.
//
// C++/Rust delta: Rust's `log` crate hides the sink and clock behind macros. Here we make
// both explicit and injectable — the sink is any std::ostream, the clock is a std::function
// — which is what lets tests assert exact output against a frozen timestamp.
#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <mutex>
#include <vector>
#include <utility>

namespace snapback {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error, Off };

inline const char* to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off: return "OFF";
    }
    return "INFO";
}

// Parse a level name (case-insensitive-ish via exact match on upper form), falling back
// to `fallback` for anything unrecognized — so a bad SNAPBACK_LOG value can't crash us.
inline LogLevel level_from_string(std::string_view name, LogLevel fallback = LogLevel::Info) {
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) upper.push_back(static_cast<char>(std::toupper((unsigned char)c)));
    if (upper == "TRACE") return LogLevel::Trace;
    if (upper == "DEBUG") return LogLevel::Debug;
    if (upper == "INFO") return LogLevel::Info;
    if (upper == "WARN" || upper == "WARNING") return LogLevel::Warn;
    if (upper == "ERROR") return LogLevel::Error;
    if (upper == "OFF" || upper == "NONE") return LogLevel::Off;
    return fallback;
}

inline std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline constexpr std::size_t kDefaultLogMaxBytes = 5 * 1024 * 1024;
inline constexpr std::size_t kDefaultLogBackups = 3;

// A stream buffer that writes complete records to a bounded log file. Logger writes a
// line in several << operations, so buffering until '\n' is important: rotation must
// never split one formatted log record across two files.
class RotatingFileBuffer final : public std::streambuf {
public:
    RotatingFileBuffer(std::filesystem::path path, std::size_t max_bytes,
                       std::size_t max_backups)
        : path_(std::move(path)),
          max_bytes_(std::max<std::size_t>(1, max_bytes)),
          max_backups_(max_backups) {
        open_append();
    }

    ~RotatingFileBuffer() override { sync(); }

    [[nodiscard]] bool healthy() const noexcept { return !failed_ && file_.is_open(); }
    [[nodiscard]] std::size_t bytes_written() const noexcept { return bytes_written_; }

protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) pending_.push_back(static_cast<char>(ch));
        return flush_complete_lines() ? traits_type::not_eof(ch) : traits_type::eof();
    }

    std::streamsize xsputn(const char* source, std::streamsize count) override {
        if (count <= 0 || failed_) return 0;
        pending_.append(source, static_cast<std::size_t>(count));
        return flush_complete_lines() ? count : 0;
    }

    int sync() override { return flush_pending() ? 0 : -1; }

private:
    std::filesystem::path backup_path(std::size_t number) const {
        return std::filesystem::path(path_.string() + "." + std::to_string(number));
    }

    void open_append() {
        std::error_code error;
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path(), error);
            if (error) {
                failed_ = true;
                return;
            }
        }

        if (std::filesystem::exists(path_, error)) {
            if (error || !std::filesystem::is_regular_file(path_, error)) {
                failed_ = true;
                return;
            }
            bytes_written_ = std::filesystem::file_size(path_, error);
            if (error) {
                failed_ = true;
                return;
            }
        }

        file_.open(path_, std::ios::binary | std::ios::app);
        failed_ = !file_.is_open();
    }

    bool flush_complete_lines() {
        while (!failed_) {
            const auto newline = pending_.find('\n');
            if (newline == std::string::npos) return true;
            const std::size_t record_size = newline + 1;
            if (!write_record(pending_.substr(0, record_size))) return false;
            pending_.erase(0, record_size);
        }
        return false;
    }

    bool flush_pending() {
        if (failed_ || pending_.empty()) return !failed_;
        const bool written = write_record(pending_);
        if (written) pending_.clear();
        return written;
    }

    bool write_record(const std::string& record) {
        if (failed_) return false;
        const bool over_limit = bytes_written_ >= max_bytes_ ||
                                record.size() > max_bytes_ - bytes_written_;
        if (bytes_written_ > 0 && over_limit) {
            if (!rotate()) return false;
        }

        file_.write(record.data(), static_cast<std::streamsize>(record.size()));
        file_.flush();
        if (!file_) {
            failed_ = true;
            return false;
        }
        bytes_written_ += record.size();
        return true;
    }

    bool rotate() {
        file_.close();
        std::error_code error;

        if (max_backups_ == 0) {
            file_.open(path_, std::ios::binary | std::ios::trunc);
            bytes_written_ = 0;
            failed_ = !file_.is_open();
            return !failed_;
        }

        for (std::size_t i = max_backups_; i > 0; --i) {
            const auto source = i == 1 ? path_ : backup_path(i - 1);
            const auto destination = backup_path(i);
            error.clear();
            if (!std::filesystem::exists(source, error)) {
                if (error) {
                    failed_ = true;
                    return false;
                }
                continue;
            }

            error.clear();
            std::filesystem::remove(destination, error);
            if (error) {
                failed_ = true;
                return false;
            }
            error.clear();
            std::filesystem::rename(source, destination, error);
            if (error) {
                failed_ = true;
                return false;
            }
        }

        file_.open(path_, std::ios::binary | std::ios::trunc);
        bytes_written_ = 0;
        failed_ = !file_.is_open();
        return !failed_;
    }

    std::filesystem::path path_;
    std::ofstream file_;
    std::string pending_;
    std::size_t max_bytes_;
    std::size_t max_backups_;
    std::size_t bytes_written_ = 0;
    bool failed_ = false;
};

// std::ostream adapter so the existing Logger API can use a rotating file without
// changing callers that already provide an ostringstream or another standard stream.
class RotatingFileStream final : public std::ostream {
public:
    explicit RotatingFileStream(const std::filesystem::path& path,
                                std::size_t max_bytes = kDefaultLogMaxBytes,
                                std::size_t max_backups = kDefaultLogBackups)
        : std::ostream(nullptr), buffer_(path, max_bytes, max_backups) {
        rdbuf(&buffer_);
        if (!buffer_.healthy()) setstate(std::ios::failbit);
    }

    ~RotatingFileStream() override { flush(); }

    [[nodiscard]] bool healthy() const noexcept { return buffer_.healthy() && !fail(); }
    [[nodiscard]] std::size_t bytes_written() const noexcept { return buffer_.bytes_written(); }

private:
    RotatingFileBuffer buffer_;
};

// Picks the sink for process-wide startup/shutdown logging: the caller's rotating file
// if it opened successfully, otherwise std::cerr so a bad log path never silences the app.
// A free function, not a class, because there's exactly one decision to make here and no
// state to own — the returned reference always outlives the RotatingFileStream passed in.
inline std::ostream& pick_startup_log_sink(RotatingFileStream& file, std::ostream& fallback) {
    return file.healthy() ? static_cast<std::ostream&>(file) : fallback;
}

class Logger {
public:
    using Clock = std::function<std::string()>;

    explicit Logger(std::ostream& sink, LogLevel min_level = LogLevel::Info,
                    Clock clock = utc_timestamp)
        : sink_(sink), min_level_(min_level), clock_(std::move(clock)) {}

    [[nodiscard]] bool enabled(LogLevel level) const {
        return min_level_ != LogLevel::Off && level >= min_level_;
    }

    void log(LogLevel level, std::string_view message) {
        if (!enabled(level)) return;
        std::string line = clock_() + " [" + to_string(level) + "] " +
                           std::string(message);
        sink_ << line << '\n';
        std::lock_guard lock(recent_mutex_);
        recent_lines_.push_back(std::move(line));
        while (recent_lines_.size() > kRecentLogLines) recent_lines_.pop_front();
    }

    void trace(std::string_view m) { log(LogLevel::Trace, m); }
    void debug(std::string_view m) { log(LogLevel::Debug, m); }
    void info(std::string_view m) { log(LogLevel::Info, m); }
    void warn(std::string_view m) { log(LogLevel::Warn, m); }
    void error(std::string_view m) { log(LogLevel::Error, m); }

    void set_level(LogLevel level) { min_level_ = level; }
    [[nodiscard]] LogLevel level() const noexcept { return min_level_; }
    [[nodiscard]] std::vector<std::string> recent_lines(std::size_t limit = kRecentLogLines) const {
        std::lock_guard lock(recent_mutex_);
        const auto count = std::min(limit, recent_lines_.size());
        std::vector<std::string> result;
        result.reserve(count);
        const auto begin = recent_lines_.size() - count;
        for (std::size_t index = begin; index < recent_lines_.size(); ++index) {
            result.push_back(recent_lines_[index]);
        }
        return result;
    }

private:
    static constexpr std::size_t kRecentLogLines = 200;
    std::ostream& sink_;
    LogLevel min_level_;
    Clock clock_;
    mutable std::mutex recent_mutex_;
    std::deque<std::string> recent_lines_;
};

}  // namespace snapback
