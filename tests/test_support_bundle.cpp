#include "doctest_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "app/support_bundle.hpp"

using namespace snapback;

namespace {

struct SupportTempDir {
    std::filesystem::path path;

    SupportTempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_support_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~SupportTempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

}  // namespace

TEST_CASE("support bundle exports only the documented diagnostics") {
    SupportTempDir temp;
    DiagnosticsSnapshot diagnostics;
    diagnostics.version = "9.8.7-test";
    diagnostics.health.status = "online";
    diagnostics.health.capture_running = true;
    diagnostics.health.classifier.model_path = "/Users/example/model.onnx";
    diagnostics.recent_logs = {"startup complete", "data dir: /private/example"};

    const auto result = export_support_bundle(temp.path, diagnostics);
    CHECK(std::filesystem::exists(result.output_path));
    CHECK(result.privacy_notice == kSupportBundlePrivacyNotice);

    std::ifstream in(result.output_path);
    const auto bundle = nlohmann::json::parse(in);
    CHECK(bundle.at("version") == "9.8.7-test");
    CHECK(bundle.at("health").at("status") == "online");
    CHECK(bundle.at("recentLogs").size() == 2);
    CHECK(bundle.at("build").contains("platform"));
    CHECK(bundle.at("build").contains("osRelease"));
    CHECK(bundle.at("build").contains("osBuild"));
    CHECK(bundle.at("build").contains("architecture"));
    const auto notice = bundle.at("privacyNotice").get<std::string>();
    CHECK(notice.find("does not contain the database") != std::string::npos);
    CHECK(notice.find("Health details and log lines may contain local file paths") !=
          std::string::npos);
    CHECK(bundle.at("health").at("classifier").at("modelPath") ==
          "/Users/example/model.onnx");
    CHECK_FALSE(bundle.contains("sessions"));
    CHECK_FALSE(bundle.contains("predictions"));
    CHECK_FALSE(bundle.contains("windowTitles"));
}
