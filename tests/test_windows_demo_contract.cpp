#include "doctest_wrapper.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef SNAPBACK_SOURCE_DIR
#define SNAPBACK_SOURCE_DIR "."
#endif

namespace {

// Reads the script with its line endings normalised to LF.
//
// The file is opened in binary deliberately — text mode would translate CRLF on Windows and
// leave it alone elsewhere, so the test would depend on the host's idea of a line rather than
// on the script. But that makes the raw bytes line-ending sensitive, and the repo has no
// `.gitattributes`, so a Windows checkout gets CRLF (the runner images default
// `core.autocrlf` to true) while every other host gets LF.
//
// That is what broke CI run 30607879815 on both Windows jobs: the one assertion below that
// spans two lines searched for "...{\n    Wait-ForFrontend\n}" against a buffer containing
// "\r\n", and failed. Every other assertion in this file is single-line, which is exactly why
// precisely one of them failed and the failure read as mysterious rather than as an encoding
// problem.
//
// Normalising here rather than adding a `.gitattributes` rule is deliberate: this test
// asserts what the script *does*, a property that has nothing to do with line endings, and
// PowerShell scripts on Windows are conventionally CRLF. Pinning the checkout to LF would fix
// the test by constraining the artifact, which is the wrong way round.
std::string read_windows_demo_script() {
    const auto path =
        std::filesystem::path(SNAPBACK_SOURCE_DIR) / "scripts/windows_demo.ps1";
    std::ifstream input(path, std::ios::binary);
    std::string contents{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
    contents.erase(std::remove(contents.begin(), contents.end(), '\r'), contents.end());
    return contents;
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
