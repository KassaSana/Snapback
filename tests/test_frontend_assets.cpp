#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "app/frontend_assets.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_frontend_assets_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_file(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "<!doctype html>";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("resolve_frontend_url uses explicit dev override first") {
    TempDir temp;
    write_file(temp.path / "frontend" / "index.html");

    CHECK(resolve_frontend_url(temp.path, "http://127.0.0.1:5173") ==
          "http://127.0.0.1:5173");
}

TEST_CASE("resolve_frontend_url can reject overrides for release builds") {
    TempDir temp;
    write_file(temp.path / "frontend" / "index.html");

    const auto resolved =
        resolve_frontend_url(temp.path, "https://untrusted.example", false);
    CHECK(resolved.rfind("file://", 0) == 0);
    CHECK(resolved.find("frontend/index.html") != std::string::npos);
}

TEST_CASE("resolve_frontend_url loads bundled frontend when present") {
    TempDir temp;
    const auto index = temp.path / "frontend" / "index.html";
    write_file(index);

    const auto resolved = resolve_frontend_url(temp.path, std::nullopt);

    CHECK(resolved.rfind("file:///", 0) == 0);
    CHECK(resolved.find("frontend/index.html") != std::string::npos);
}

TEST_CASE("resolve_frontend_url falls back to Vite when bundle is absent") {
    TempDir temp;

    CHECK(resolve_frontend_url(temp.path, std::nullopt) == "http://localhost:5173");
}

TEST_CASE("resolve_frontend_url fails closed when a release bundle is absent") {
    TempDir temp;

    CHECK(resolve_frontend_url(temp.path, std::nullopt, false, false) == "about:blank");
}

// A `file://` URL has exactly three slashes before the path: two for the empty authority
// and one starting an absolute path. On POSIX the path already begins with `/`, so naive
// concatenation onto "file:///" yields FOUR — an empty authority followed by a `//Users/...`
// path, which WKWebView and WebKitGTK both refuse to load. That shipped: the macOS app
// opened a blank window while every test passed, because the tests only asserted the
// "file:///" prefix (which a four-slash URL also satisfies) and never the whole URL.
TEST_CASE("file_url_from_path emits exactly three slashes for an absolute path") {
    const auto url = file_url_from_path(std::filesystem::path("/tmp/sb/frontend/index.html"));

#if defined(_WIN32)
    // Windows absolute paths start with a drive letter, so the third slash is the separator.
    CHECK(url.rfind("file:///", 0) == 0);
    CHECK(url.rfind("file:////", 0) != 0);
#else
    CHECK(url == "file:///tmp/sb/frontend/index.html");
#endif
    // Stated independently of the platform: never four.
    CHECK(url.rfind("file:////", 0) != 0);
}

TEST_CASE("file_url_from_path escapes spaces") {
    TempDir temp;
    const auto path = temp.path / "with space" / "index.html";
    write_file(path);

    CHECK(file_url_from_path(path).find("with%20space") != std::string::npos);
}

TEST_CASE("bundled frontend declares a restrictive content security policy") {
#ifndef SNAPBACK_SOURCE_DIR
#define SNAPBACK_SOURCE_DIR "."
#endif
    const auto html = read_file(std::filesystem::path(SNAPBACK_SOURCE_DIR) / "frontend" /
                                "index.html");
    CHECK(html.find("http-equiv=\"Content-Security-Policy\"") != std::string::npos);
    CHECK(html.find("default-src 'self'; script-src 'self'") != std::string::npos);
}
