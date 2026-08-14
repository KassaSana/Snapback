#include "doctest_wrapper.hpp"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/file_dialog.hpp"

using json = nlohmann::json;

TEST_SUITE("file_dialog") {

TEST_CASE("file_dialog_supported reflects platform capabilities") {
#if defined(_WIN32) || defined(__APPLE__)
    CHECK(snapback::file_dialog_supported() == true);
#else
    CHECK(snapback::file_dialog_supported() == false);
#endif
}

TEST_CASE("FileDialogFilter serializes and deserializes cleanly") {
    snapback::FileDialogFilter filter{"Database Files (*.db)", "*.db"};
    json j = filter;
    CHECK(j.at("name") == "Database Files (*.db)");
    CHECK(j.at("pattern") == "*.db");

    auto roundtrip = j.get<snapback::FileDialogFilter>();
    CHECK(roundtrip.name == "Database Files (*.db)");
    CHECK(roundtrip.pattern == "*.db");
}

TEST_CASE("FileDialogOptions round-trips through JSON") {
    snapback::FileDialogOptions options;
    options.title = "Select Database";
    options.default_path = "C:/Users/Test/Downloads";
    options.default_name = "focoflow.db";
    options.filters = {{"Database Files (*.db)", "*.db"}, {"All Files (*.*)", "*.*"}};

    json j = options;
    CHECK(j.at("title") == "Select Database");
    CHECK(j.at("defaultPath") == "C:/Users/Test/Downloads");
    CHECK(j.at("defaultName") == "focoflow.db");
    CHECK(j.at("filters").size() == 2);

    auto roundtrip = j.get<snapback::FileDialogOptions>();
    CHECK(roundtrip.title == options.title);
    CHECK(roundtrip.default_path == options.default_path);
    CHECK(roundtrip.default_name == options.default_name);
    REQUIRE(roundtrip.filters.size() == 2);
    CHECK(roundtrip.filters[0].name == "Database Files (*.db)");
    CHECK(roundtrip.filters[1].pattern == "*.*");
}

TEST_CASE("FileDialogResult carries ok, cancelled, path, and message") {
    snapback::FileDialogResult res{true, false, "C:/Path/To/file.db", ""};
    json j = res;
    CHECK(j.at("ok") == true);
    CHECK(j.at("cancelled") == false);
    CHECK(j.at("path") == "C:/Path/To/file.db");
    CHECK(j.at("message") == "");

    snapback::FileDialogResult cancelled_res{false, true, "", "Cancelled by user"};
    json j2 = cancelled_res;
    CHECK(j2.at("ok") == false);
    CHECK(j2.at("cancelled") == true);
    CHECK(j2.at("message") == "Cancelled by user");
}

}  // TEST_SUITE
