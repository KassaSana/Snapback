#include "doctest_wrapper.hpp"

#include "app/open_url.hpp"
#include "app/webview_origin.hpp"

using namespace snapback;

TEST_CASE("external_url_argv keeps the URL as one argument, never shell text") {
    const auto argv = external_url_argv("https://example.com/a;b?c=d");
    REQUIRE(argv.size() == 2);
    CHECK(argv[0] == "xdg-open");
    CHECK(argv[1] == "https://example.com/a;b?c=d");
}

TEST_CASE("open_allowed_external_url accepts http(s)/mailto and rejects dangerous schemes") {
    CHECK(detail::open_allowed_external_url("https://example.com/help"));
    CHECK(detail::open_allowed_external_url("http://127.0.0.1:8080/docs"));
    CHECK(detail::open_allowed_external_url("mailto:support@example.com"));
    CHECK_FALSE(detail::open_allowed_external_url("javascript:alert(1)"));
    CHECK_FALSE(detail::open_allowed_external_url("file:///etc/passwd"));
    CHECK_FALSE(detail::open_allowed_external_url(""));
}

TEST_CASE("open_external_url refuses blocked schemes without calling the OS") {
    CHECK_FALSE(open_external_url("javascript:alert(1)"));
    CHECK_FALSE(open_external_url("file:///etc/passwd"));
}

TEST_CASE("open_external_url_supported matches platforms with a backend") {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    CHECK(open_external_url_supported());
#else
    CHECK_FALSE(open_external_url_supported());
#endif
}
