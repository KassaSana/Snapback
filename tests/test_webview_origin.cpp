#include "doctest_wrapper.hpp"

#include "app/webview_origin.hpp"

using namespace snapback;

TEST_CASE("canonical_document_url strips query and fragment") {
    CHECK(canonical_document_url("file:///app/index.html?x=1#top") ==
          canonical_document_url("file:///app/index.html"));
}

TEST_CASE("canonical_document_url collapses file path aliases") {
    const auto direct = canonical_document_url("file:///app/frontend/index.html");
    const auto alias =
        canonical_document_url("file:///app/frontend/../frontend/index.html");
    CHECK(direct == alias);
}

TEST_CASE("canonical_document_url lowercases file paths on Windows-style separators") {
    const auto url = canonical_document_url("file:///C:/Snapback/frontend/index.html");
    CHECK(url.find('\\') == std::string::npos);
    CHECK(url.rfind("file:///c:/snapback/frontend/index.html", 0) == 0);
}

TEST_CASE("canonical_document_url normalizes http authority case") {
    CHECK(canonical_document_url("HTTP://LOCALHOST:5173/src/main.tsx") ==
          "http://localhost:5173/src/main.tsx");
}

TEST_CASE("is_trusted_document accepts the canonical packaged URL") {
    const std::string trusted = "file:///tmp/snapback/frontend/index.html";
    CHECK(is_trusted_document(trusted, trusted, false));
    CHECK(is_trusted_document(trusted + "?v=1", trusted, false));
}

TEST_CASE("is_trusted_document rejects a different file URL") {
    const std::string trusted = "file:///tmp/snapback/frontend/index.html";
    CHECK_FALSE(is_trusted_document("file:///tmp/evil/index.html", trusted, false));
}

TEST_CASE("is_trusted_document allows loopback only in debug builds") {
    const std::string trusted = "file:///tmp/snapback/frontend/index.html";
    CHECK_FALSE(is_trusted_document("http://localhost:5173/", trusted, false));
    CHECK(is_trusted_document("http://localhost:5173/", trusted, true));
    CHECK(is_trusted_document("http://127.0.0.1:5173/", trusted, true));
    CHECK_FALSE(is_trusted_document("http://localhost.evil.com/", trusted, true));
}

TEST_CASE("classify_navigation allows trusted, externalizes http(s)/mailto, blocks the rest") {
    const std::string trusted = "file:///tmp/snapback/frontend/index.html";
    CHECK(classify_navigation(trusted, trusted, false) == NavigationDecision::Allow);
    CHECK(classify_navigation("https://example.com/docs", trusted, false) ==
          NavigationDecision::OpenExternally);
    CHECK(classify_navigation("mailto:support@example.com", trusted, false) ==
          NavigationDecision::OpenExternally);
    CHECK(classify_navigation("javascript:alert(1)", trusted, false) ==
          NavigationDecision::Block);
    CHECK(classify_navigation("file:///etc/passwd", trusted, false) ==
          NavigationDecision::Block);
}

TEST_CASE("generate_capability_token emits 64 lowercase hex characters") {
    const auto token = generate_capability_token();
    REQUIRE(token.size() == 64);
    for (char c : token) {
        CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
    }
    CHECK(token != generate_capability_token());
}
