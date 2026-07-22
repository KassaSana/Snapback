#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "util/logger.hpp"

using namespace snapback;

namespace {
// Frozen clock so lines are deterministic.
Logger::Clock fixed_clock() {
    return [] { return std::string("2026-07-18T00:00:00Z"); };
}

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_logger_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
}  // namespace

TEST_CASE("Logger formats a line with timestamp, level, and message") {
    std::ostringstream out;
    Logger log(out, LogLevel::Info, fixed_clock());
    log.info("engine started");
    CHECK(out.str() == "2026-07-18T00:00:00Z [INFO] engine started\n");
    CHECK(log.recent_lines() == std::vector<std::string>{"2026-07-18T00:00:00Z [INFO] engine started"});
}

TEST_CASE("Logger keeps a bounded diagnostics tail") {
    std::ostringstream out;
    Logger log(out, LogLevel::Info, fixed_clock());
    for (int i = 0; i < 205; ++i) log.info("line " + std::to_string(i));

    const auto recent = log.recent_lines(3);
    REQUIRE(recent.size() == 3);
    CHECK(recent[0].find("line 202") != std::string::npos);
    CHECK(recent[2].find("line 204") != std::string::npos);
}

TEST_CASE("Logger drops messages below the configured level") {
    std::ostringstream out;
    Logger log(out, LogLevel::Warn, fixed_clock());
    log.info("noise");   // suppressed
    log.debug("noise");  // suppressed
    log.warn("heads up");
    log.error("boom");
    CHECK(out.str() ==
          "2026-07-18T00:00:00Z [WARN] heads up\n"
          "2026-07-18T00:00:00Z [ERROR] boom\n");
}

TEST_CASE("Logger Off level silences everything") {
    std::ostringstream out;
    Logger log(out, LogLevel::Off, fixed_clock());
    log.error("still quiet");
    CHECK(out.str().empty());
    CHECK_FALSE(log.enabled(LogLevel::Error));
}

TEST_CASE("level_from_string parses names and falls back safely") {
    CHECK(level_from_string("debug") == LogLevel::Debug);
    CHECK(level_from_string("WARNING") == LogLevel::Warn);
    CHECK(level_from_string("off") == LogLevel::Off);
    CHECK(level_from_string("nonsense", LogLevel::Error) == LogLevel::Error);
}

TEST_CASE("Logger set_level takes effect immediately") {
    std::ostringstream out;
    Logger log(out, LogLevel::Error, fixed_clock());
    log.info("before");  // dropped
    log.set_level(LogLevel::Info);
    log.info("after");
    CHECK(out.str() == "2026-07-18T00:00:00Z [INFO] after\n");
}

TEST_CASE("RotatingFileStream creates parent directories and writes complete records") {
    TempDir temp;
    const auto path = temp.path / "logs" / "snapback.log";

    {
        RotatingFileStream file(path, 1024, 2);
        REQUIRE(file.healthy());
        Logger log(file, LogLevel::Info, fixed_clock());
        log.info("file sink works");
        file.flush();
    }

    CHECK(std::filesystem::exists(path));
    CHECK(read_file(path) == "2026-07-18T00:00:00Z [INFO] file sink works\n");
}

TEST_CASE("RotatingFileStream rotates before a record exceeds the byte limit") {
    TempDir temp;
    const auto path = temp.path / "snapback.log";
    const std::string first = "2026-07-18T00:00:00Z [INFO] first\n";
    const std::string second = "2026-07-18T00:00:00Z [INFO] second\n";

    {
        RotatingFileStream file(path, first.size() + 1, 2);
        REQUIRE(file.healthy());
        Logger log(file, LogLevel::Info, fixed_clock());
        log.info("first");
        log.info("second");
        file.flush();
    }

    CHECK(read_file(path) == second);
    CHECK(read_file(path.string() + ".1") == first);
    CHECK_FALSE(std::filesystem::exists(path.string() + ".2"));
}

TEST_CASE("pick_startup_log_sink prefers a healthy file over the fallback") {
    TempDir temp;
    RotatingFileStream file(temp.path / "snapback.log");
    REQUIRE(file.healthy());
    std::ostringstream fallback;

    std::ostream& sink = pick_startup_log_sink(file, fallback);
    Logger log(sink, LogLevel::Info, fixed_clock());
    log.info("routed to file");
    file.flush();

    CHECK(read_file(temp.path / "snapback.log") == "2026-07-18T00:00:00Z [INFO] routed to file\n");
    CHECK(fallback.str().empty());
}

TEST_CASE("pick_startup_log_sink falls back when the file can't be opened") {
    // A directory can never be opened as a regular file, so RotatingFileStream fails and
    // healthy() is false — this is the "bad log path" case the sink must degrade from.
    TempDir temp;
    RotatingFileStream file(temp.path);
    REQUIRE_FALSE(file.healthy());
    std::ostringstream fallback;

    std::ostream& sink = pick_startup_log_sink(file, fallback);
    Logger log(sink, LogLevel::Info, fixed_clock());
    log.info("routed to fallback");

    CHECK(fallback.str() == "2026-07-18T00:00:00Z [INFO] routed to fallback\n");
}

TEST_CASE("RotatingFileStream keeps only the configured number of backups") {
    TempDir temp;
    const auto path = temp.path / "snapback.log";

    {
        RotatingFileStream file(path, 1, 2);
        REQUIRE(file.healthy());
        file << "a\n" << "b\n" << "c\n";
        file.flush();
    }

    CHECK(read_file(path) == "c\n");
    CHECK(read_file(path.string() + ".1") == "b\n");
    CHECK(read_file(path.string() + ".2") == "a\n");
    CHECK_FALSE(std::filesystem::exists(path.string() + ".3"));
}
