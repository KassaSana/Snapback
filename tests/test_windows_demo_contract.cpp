#include "doctest_wrapper.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef SNAPBACK_SOURCE_DIR
#define SNAPBACK_SOURCE_DIR "."
#endif

namespace {

// Reads the script with line endings normalised to LF, in binary mode, so multi-line assertions
// hold on CRLF checkouts. .gitattributes deliberately leaves scripts/*.ps1 alone (PowerShell is
// conventionally CRLF).
std::string read_script(const std::filesystem::path& relative_path) {
    const auto path = std::filesystem::path(SNAPBACK_SOURCE_DIR) / relative_path;
    std::ifstream input(path, std::ios::binary);
    std::string contents{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
    contents.erase(std::remove(contents.begin(), contents.end(), '\r'), contents.end());
    return contents;
}

}  // namespace

TEST_CASE("Windows Vite demo selects Debug and probes the local server") {
    const auto script = read_script("scripts/windows_demo.ps1");

    CHECK(script.find("$BuildConfig = if ($UseVite) { \"Debug\" } else { \"Release\" }") !=
          std::string::npos);
    CHECK(script.find("cmake --build $BuildPath --config $BuildConfig --target snapback") !=
          std::string::npos);
    CHECK(script.find("ctest --test-dir $BuildPath -C $BuildConfig") != std::string::npos);
    CHECK(script.find("Assert-LocalFrontendUrl") != std::string::npos);
    CHECK(script.find("if ($UseVite) {\n    Wait-ForFrontend\n}") != std::string::npos);
    CHECK(script.find("--config Release --target snapback") == std::string::npos);
}

TEST_CASE("Windows GUI smoke finds the configuration selected by its caller") {
    const auto script = read_script("scripts/gui_smoke_windows.ps1");

    CHECK(script.find("(Join-Path $BuildPath \"$Config\\snapback.exe\")") !=
          std::string::npos);
}

TEST_CASE("Windows driven GUI smoke isolates its WebView2 browser profile") {
    const auto script = read_script("scripts/gui_smoke_windows.ps1");

    CHECK(script.find(
              "$env:WEBVIEW2_USER_DATA_FOLDER = Join-Path $DemoDataDir \"webview2-cdp-$cdpPort\"") !=
          std::string::npos);
    CHECK(script.find(
              "$previousWebViewUserDataFolder = [Environment]::GetEnvironmentVariable(\"WEBVIEW2_USER_DATA_FOLDER\")") !=
          std::string::npos);
    CHECK(script.find("Remove-Item Env:\\WEBVIEW2_USER_DATA_FOLDER -ErrorAction SilentlyContinue") !=
          std::string::npos);
}
