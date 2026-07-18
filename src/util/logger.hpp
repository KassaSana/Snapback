// Structured leveled logger. Roadmap 4.1 (first slice: levels + formatting + filtering).
//
// Replaces scattered std::cerr writes with one leveled sink: every line is
// "<timestamp> [LEVEL] message", and anything below the configured level is dropped.
// File rotation + an in-app diagnostics view are follow-ups; this is the core others log
// through.
//
// C++/Rust delta: Rust's `log` crate hides the sink and clock behind macros. Here we make
// both explicit and injectable — the sink is any std::ostream, the clock is a std::function
// — which is what lets tests assert exact output against a frozen timestamp.
#pragma once

#include <cctype>
#include <ctime>
#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

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
        sink_ << clock_() << " [" << to_string(level) << "] " << message << '\n';
    }

    void trace(std::string_view m) { log(LogLevel::Trace, m); }
    void debug(std::string_view m) { log(LogLevel::Debug, m); }
    void info(std::string_view m) { log(LogLevel::Info, m); }
    void warn(std::string_view m) { log(LogLevel::Warn, m); }
    void error(std::string_view m) { log(LogLevel::Error, m); }

    void set_level(LogLevel level) { min_level_ = level; }
    [[nodiscard]] LogLevel level() const noexcept { return min_level_; }

private:
    std::ostream& sink_;
    LogLevel min_level_;
    Clock clock_;
};

}  // namespace snapback
