#include "doctest_wrapper.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#ifndef SNAPBACK_SOURCE_DIR
#define SNAPBACK_SOURCE_DIR "."
#endif

namespace {

std::string read_windows_demo_script() {
    const auto path =
        std::filesystem::path(SNAPBACK_SOURCE_DIR) / "scripts/windows_demo.ps1";
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("Windows Vite demo selects Debug and probes the local server") {
    const auto script = read_windows_demo_script();

    CHECK(script.find("$BuildConfig = if ($UseVite) { \"Debug\" } else { \"Release\" }") !=
          std::string::npos);
    CHECK(script.find("cmake --build $BuildPath --config $BuildConfig --target snapback") !=
          std::string::npos);
    CHECK(script.find("ctest --test-dir $BuildPath -C $BuildConfig") != std::string::npos);
    CHECK(script.find("Assert-LocalFrontendUrl") != std::string::npos);
    CHECK(script.find("if ($UseVite) {\n    Wait-ForFrontend\n}") != std::string::npos);
    CHECK(script.find("--config Release --target snapback") == std::string::npos);
}
