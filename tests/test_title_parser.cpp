// The title parser has a focused doctest suite with real, runnable examples.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest_wrapper.hpp"

#include "snapback/title_parser.hpp"

using snapback::parse_title;

TEST_CASE("parses VS Code em-dash titles into file + project") {
    auto hints = parse_title("auth.ts \xE2\x80\x94 Snapback \xE2\x80\x94 Visual Studio Code");
    CHECK(hints.file_hint == "auth.ts");
    CHECK(hints.project_hint == "Snapback");
}

TEST_CASE("falls back to the whole title when there is no separator") {
    auto hints = parse_title("Untitled");
    CHECK(hints.file_hint == "Untitled");
    CHECK(hints.project_hint.empty());
}
