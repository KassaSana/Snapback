#include <doctest/doctest.h>

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

}  // namespace

TEST_CASE("resolve_frontend_url uses explicit dev override first") {
    TempDir temp;
    write_file(temp.path / "frontend" / "index.html");

    CHECK(resolve_frontend_url(temp.path, "http://127.0.0.1:5173") ==
          "http://127.0.0.1:5173");
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

TEST_CASE("file_url_from_path escapes spaces") {
    TempDir temp;
    const auto path = temp.path / "with space" / "index.html";
    write_file(path);

    CHECK(file_url_from_path(path).find("with%20space") != std::string::npos);
}
