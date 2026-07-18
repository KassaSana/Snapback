#include <doctest/doctest.h>

#include <sstream>

#include "util/logger.hpp"

using namespace snapback;

namespace {
// Frozen clock so lines are deterministic.
Logger::Clock fixed_clock() {
    return [] { return std::string("2026-07-18T00:00:00Z"); };
}
}  // namespace

TEST_CASE("Logger formats a line with timestamp, level, and message") {
    std::ostringstream out;
    Logger log(out, LogLevel::Info, fixed_clock());
    log.info("engine started");
    CHECK(out.str() == "2026-07-18T00:00:00Z [INFO] engine started\n");
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
