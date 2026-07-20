#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "app/training_deploy.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_training_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

}  // namespace

TEST_CASE("training_deploy detects Snapback repo by pipeline_cli") {
    TempDir temp;

    CHECK_FALSE(training_deploy::is_training_repo(temp.path));
    write_text(temp.path / "ml" / "pipeline_cli.py", "# test\n");
    CHECK(training_deploy::is_training_repo(temp.path));
}

TEST_CASE("training_deploy stores and reads configured repo path") {
    TempDir app_data;
    TempDir repo;
    write_text(repo.path / "ml" / "pipeline_cli.py", "# test\n");

    training_deploy::write_training_repo_path(app_data.path, repo.path);
    auto configured = training_deploy::read_training_repo_path(app_data.path);

    REQUIRE(configured.has_value());
    CHECK(*configured == repo.path);
}

TEST_CASE("training_deploy status counts export rows labels metrics and model") {
    TempDir app_data;
    const auto out_dir = training_deploy::export_dir(app_data.path);
    write_text(out_dir / "features.csv", "timestamp,feature\n1,2\n3,4\n");
    write_text(out_dir / "labels.csv",
               "timestamp,label,source,session_id,notes\n"
               "1,-1,manual,s1,\n"
               "2,1,manual,s1,\n"
               "3,1,manual,s1,\n"
               "4,2,manual,s1,\n");
    write_text(out_dir / "metrics.json",
               "{\"cv_accuracy\":0.75,\"ignored\":\"text\",\"recall_distracted\":0.5}");
    write_text(out_dir / "model.onnx", "fake");

    auto status = training_deploy::training_deploy_status(app_data.path);

    CHECK(status.at("exportDir").get<std::string>() == out_dir.string());
    CHECK(status.at("featureCount").get<std::uint64_t>() == 2);
    CHECK(status.at("labelCount").get<std::uint64_t>() == 4);
    CHECK(status.at("hasExport").get<bool>());
    CHECK(status.at("modelOnnxExists").get<bool>());
    CHECK(status.at("metricsExists").get<bool>());
    CHECK(status.at("labelBreakdown").at("DISTRACTED").get<int>() == 1);
    CHECK(status.at("labelBreakdown").at("PRODUCTIVE").get<int>() == 2);
    CHECK(status.at("labelBreakdown").at("DEEP_FOCUS").get<int>() == 1);
    CHECK(status.at("metrics").at("cv_accuracy").get<double>() == doctest::Approx(0.75));
    CHECK(status.at("metrics").contains("ignored") == false);
}

TEST_CASE("training_deploy rejects invalid configured repo") {
    TempDir app_data;
    TempDir not_repo;

    CHECK_THROWS_AS(training_deploy::write_training_repo_path(app_data.path, not_repo.path),
                    std::runtime_error);
}

TEST_CASE("training_deploy builds platform command with output dir") {
    const auto command = training_deploy::build_pipeline_command("C:/app data/exports/training");
    CHECK(command.find("-m ml.pipeline_cli") != std::string::npos);
    CHECK(command.find("--skip-export") != std::string::npos);
    // Quoting is platform-specific: this string is meant to be pasted into the user's own
    // shell, and POSIX shells need single quotes for the path to stay literal.
#if defined(_WIN32)
    CHECK(command.find("\"C:/app data/exports/training\"") != std::string::npos);
#else
    CHECK(command.find("'C:/app data/exports/training'") != std::string::npos);
#endif
}

TEST_CASE("shell_quote neutralizes command substitution in repo paths") {
    // The repo path comes from SNAPBACK_REPO or training_repo.txt — both writable by any
    // local process running as the user — and ends up inside a std::system() command.
    // A directory literally named "$(...)" passes is_training_repo()'s existence check,
    // because std::filesystem treats the name as literal while the shell does not.
    const auto quoted = training_deploy::detail::shell_quote("/tmp/$(touch /tmp/pwned)");

#if defined(_WIN32)
    CHECK(quoted == "\"/tmp/$(touch /tmp/pwned)\"");
#else
    // Single-quoted: the shell performs no substitution whatsoever inside these.
    CHECK(quoted == "'/tmp/$(touch /tmp/pwned)'");
    CHECK(quoted.front() == '\'');
    CHECK(quoted.back() == '\'');
#endif
}

TEST_CASE("shell_quote survives quotes in the path itself") {
    // The escape must not let the argument terminate early — that's what would reopen the
    // injection it exists to close.
#if defined(_WIN32)
    CHECK(training_deploy::detail::shell_quote("a\"b") == "\"a\"\"b\"");
#else
    CHECK(training_deploy::detail::shell_quote("a'b") == "'a'\\''b'");
    // Backticks are the other POSIX substitution syntax; single quotes cover them too.
    CHECK(training_deploy::detail::shell_quote("`id`") == "'`id`'");
#endif
}

TEST_CASE("shell_quote leaves ordinary paths usable") {
    const auto quoted = training_deploy::detail::shell_quote("/home/kassa/Snapback");
#if defined(_WIN32)
    CHECK(quoted == "\"/home/kassa/Snapback\"");
#else
    CHECK(quoted == "'/home/kassa/Snapback'");
#endif
}

TEST_CASE("normalized_exit_code unwraps the POSIX wait status") {
    using training_deploy::detail::normalized_exit_code;

    // The bug: std::system returns a wait status on POSIX, so a child exiting 2 arrives as
    // 512 and the `exit_code == 2` branch that surfaces the majority-classifier-stub
    // guidance never fired. Verified against a real `sh -c 'exit 2'`, which returns 512.
    CHECK(normalized_exit_code(0) == 0);
#if defined(_WIN32)
    CHECK(normalized_exit_code(2) == 2);
#else
    CHECK(normalized_exit_code(512) == 2);   // 2 << 8
    CHECK(normalized_exit_code(256) == 1);
    CHECK(normalized_exit_code(9) == 128 + 9);  // killed by SIGKILL, shell convention
#endif
    CHECK(normalized_exit_code(-1) == -1);  // shell never started
}

TEST_CASE("normalized_exit_code agrees with a real child process") {
    // Ties the unit test to reality rather than to my reading of the man page.
    using training_deploy::detail::normalized_exit_code;
    CHECK(normalized_exit_code(std::system("exit 2")) == 2);
    CHECK(normalized_exit_code(std::system("exit 0")) == 0);
}
