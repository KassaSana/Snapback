#include "doctest_wrapper.hpp"

#include "capture/active_window.hpp"

using namespace snapback;

TEST_CASE("Windows wide strings convert without retaining the terminator") {
#if defined(_WIN32)
    CHECK(detail::utf8_from_wide(L"Code.exe") == "Code.exe");
    CHECK(detail::utf8_from_wide(L"Résumé — 工作") == "Résumé — 工作");
    CHECK(detail::utf8_from_wide(L"").empty());
    CHECK(detail::utf8_from_wide(nullptr).empty());
#else
    // The conversion helper is intentionally Windows-only; keeping the test case
    // present makes the platform-specific coverage visible in the shared suite.
    CHECK(true);
#endif
}
