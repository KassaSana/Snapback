// Readable instants for tests, in the representation ADR-0007 stores.
//
// A test that says `ms("2026-08-09T10:00:00Z")` still tells the reader when it means, while
// handing the code under test the epoch milliseconds it actually speaks. The alternative --
// writing 1786334400000 in the fixtures -- would be correct and unreadable, and a wrong digit
// in it would look exactly like a right one.
//
// This lives under tests/ rather than src/ for the same reason `manual_clock.hpp` does: the
// shipping binary has no business carrying a helper that exists to make literals legible.
#pragma once

#include <cstdint>
#include <string>

#include "doctest_wrapper.hpp"
#include "util/time.hpp"

namespace snapback {

// Aborts the case rather than returning a sentinel. A malformed literal here is a typo in the
// test, not a condition under test, and quietly substituting the epoch would turn it into a
// confusing assertion failure somewhere far away.
inline std::int64_t ms(const std::string& rfc3339) {
    const auto value = unix_ms_from_rfc3339(rfc3339);
    REQUIRE_MESSAGE(value.has_value(), "not an RFC3339 UTC literal: " << rfc3339);
    return *value;
}

}  // namespace snapback
