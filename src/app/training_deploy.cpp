#include "app/training_deploy.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>  // WIFEXITED / WEXITSTATUS — std::system returns a wait status here
#endif

namespace snapback::training_deploy {

namespace detail {

int normalized_exit_code(int system_result) {
    if (system_result == -1) return -1;  // couldn't even start a shell
#if defined(_WIN32)
    // cmd.exe returns the child's exit code directly.
    return system_result;
#else
    // POSIX std::system returns a *wait status*, not an exit code: a child exiting 2 comes
    // back as 512 (2 << 8). The pipeline's `exit_code == 2` check for the
    // majority-classifier stub therefore never fired, and users lost the "capture more
    // labeled sessions" guidance. (`== 0` worked only because status 0 <=> exit 0.)
    if (WIFEXITED(system_result)) return WEXITSTATUS(system_result);
    if (WIFSIGNALED(system_result)) return 128 + WTERMSIG(system_result);  // shell convention
    return -1;
#endif
}

std::string shell_quote(const std::string& value) {
#if defined(_WIN32)
    // cmd.exe has no command substitution, so double quotes are enough to keep spaces and
    // operators (&, |, >) literal. Embedded quotes are doubled, which is how cmd escapes
    // them inside a quoted string.
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
#else
    // Single quotes are the only POSIX construct that suppresses *everything* — no
    // parameter expansion, no command substitution, no backslash escapes. The previous
    // version used double quotes and escaped only `"`, which left $(...) and `...` live:
    // a repo directory literally named `$(cmd)` passed the is_training_repo() existence
    // check and then executed when the path was pasted into the shell command.
    //
    // A single quote can't be escaped inside single quotes, so close, emit an escaped
    // quote, and reopen: foo'bar -> 'foo'\''bar'
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

}  // namespace detail

namespace {

std::string quote(const std::filesystem::path& path) { return detail::shell_quote(path.string()); }

std::string quote_arg(const std::string& value) { return detail::shell_quote(value); }

std::uint64_t count_csv_rows(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return 0;
    std::uint64_t lines = 0;
    std::string line;
    while (std::getline(in, line)) ++lines;
    return lines > 0 ? lines - 1 : 0;
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (c == ',' && !quoted) {
            cells.push_back(cell);
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells.push_back(cell);
    return cells;
}

nlohmann::json count_label_breakdown(const std::filesystem::path& path) {
    std::ifstream in(path);
    nlohmann::json counts = nlohmann::json::object();
    if (!in) return counts;

    std::string header;
    if (!std::getline(in, header)) return counts;
    const auto columns = split_csv_line(header);
    std::size_t label_col = std::string::npos;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] == "label") {
            label_col = i;
            break;
        }
    }
    if (label_col == std::string::npos) return counts;

    std::string line;
    while (std::getline(in, line)) {
        const auto cells = split_csv_line(line);
        if (label_col >= cells.size()) continue;
        const std::string& raw = cells[label_col];
        std::string label;
        if (raw == "-1" || raw == "DISTRACTED") label = "DISTRACTED";
        else if (raw == "0" || raw == "PSEUDO_PRODUCTIVE") label = "PSEUDO_PRODUCTIVE";
        else if (raw == "1" || raw == "PRODUCTIVE") label = "PRODUCTIVE";
        else if (raw == "2" || raw == "DEEP_FOCUS") label = "DEEP_FOCUS";
        else continue;
        counts[label] = counts.value(label, 0) + 1;
    }
    return counts;
}

std::optional<nlohmann::json> parse_metrics_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    if (!parsed.is_object()) return std::nullopt;

    nlohmann::json metrics = nlohmann::json::object();
    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        if (it.value().is_number()) metrics[it.key()] = it.value();
    }
    return metrics;
}

bool command_succeeds(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

std::optional<std::string> get_env_var(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return std::nullopt;
    std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
    return std::string(owned.get());
#else
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
#endif
}

struct PythonCommand {
    std::string program;
    std::vector<std::string> prefix_args;
};

std::optional<PythonCommand> find_python() {
#if defined(_WIN32)
    if (command_succeeds("py -3 --version >NUL 2>NUL")) return PythonCommand{"py", {"-3"}};
    if (command_succeeds("python --version >NUL 2>NUL")) return PythonCommand{"python", {}};
#else
    if (command_succeeds("python3 --version >/dev/null 2>/dev/null")) {
        return PythonCommand{"python3", {}};
    }
    if (command_succeeds("python --version >/dev/null 2>/dev/null")) return PythonCommand{"python", {}};
#endif
    return std::nullopt;
}

std::string read_file_tail(const std::filesystem::path& path, std::size_t max_lines) {
    std::ifstream in(path);
    if (!in) return "";
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
        if (lines.size() > max_lines) lines.erase(lines.begin());
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out << '\n';
        out << lines[i];
    }
    return out.str();
}

std::string build_failure_message(int exit_code, const std::string& log_tail) {
    std::string lower = log_tail;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (exit_code == 2 || lower.find("majority-classifier stub") != std::string::npos ||
        lower.find("majority stub") != std::string::npos) {
        return "Training stopped because the current data only produced a majority-classifier "
               "stub. Capture more labeled sessions, then train again.";
    }
    if (lower.find("xgboost is not installed") != std::string::npos ||
        lower.find("install onnx export deps") != std::string::npos ||
        lower.find("python not found") != std::string::npos) {
        return "Training failed. Install deps: pip install xgboost onnxmltools onnx (see log).";
    }
    return "Training failed. Check the training log for details.";
}

bool sync_trained_model_to_app_dir(const std::filesystem::path& app_data_dir,
                                   const std::filesystem::path& export_path) {
    const auto export_model = export_path / "model.onnx";
    if (!std::filesystem::is_regular_file(export_model)) return false;
    std::filesystem::create_directories(app_data_dir);
    std::filesystem::copy_file(export_model, app_data_dir / "model.onnx",
                               std::filesystem::copy_options::overwrite_existing);
    return true;
}

}  // namespace

std::filesystem::path export_dir(const std::filesystem::path& app_data_dir) {
    return app_data_dir / "exports" / "training";
}

bool is_training_repo(const std::filesystem::path& path) {
    return std::filesystem::is_regular_file(path / "ml" / "pipeline_cli.py");
}

std::optional<std::filesystem::path> read_training_repo_path(
    const std::filesystem::path& app_data_dir) {
    if (auto env = get_env_var("SNAPBACK_REPO")) {
        std::filesystem::path path(*env);
        if (is_training_repo(path)) return path;
    }

    std::ifstream in(app_data_dir / "training_repo.txt");
    if (!in) return std::nullopt;
    std::string content;
    std::getline(in, content);
    if (content.empty()) return std::nullopt;
    std::filesystem::path path(content);
    if (is_training_repo(path)) return path;
    return std::nullopt;
}

void write_training_repo_path(const std::filesystem::path& app_data_dir,
                              const std::filesystem::path& repo_path) {
    if (!is_training_repo(repo_path)) {
        throw std::runtime_error("Not a Snapback repo (missing ml/pipeline_cli.py): " +
                                 repo_path.string());
    }
    std::filesystem::create_directories(app_data_dir);
    std::ofstream out(app_data_dir / "training_repo.txt", std::ios::trunc);
    if (!out) throw std::runtime_error("Could not write training_repo.txt");
    out << repo_path.string();
}

std::string build_pipeline_command(const std::filesystem::path& output_dir) {
#if defined(_WIN32)
    const std::string python = "py -3";
#else
    const std::string python = "python3";
#endif
    return "# Run from your Snapback repo root:\n" + python +
           " -m ml.pipeline_cli \\\n  --output-dir " + quote(output_dir) +
           " \\\n  --skip-export";
}

nlohmann::json training_deploy_status(const std::filesystem::path& app_data_dir) {
    const auto out_dir = export_dir(app_data_dir);
    const auto features_path = out_dir / "features.csv";
    const auto labels_path = out_dir / "labels.csv";
    const auto metrics_path = out_dir / "metrics.json";
    const auto metrics = parse_metrics_json(metrics_path);
    const auto repo_path = read_training_repo_path(app_data_dir);
    const std::uint64_t feature_count = count_csv_rows(features_path);
    const std::uint64_t label_count = count_csv_rows(labels_path);

    return nlohmann::json{
        {"exportDir", out_dir.string()},
        {"featureCount", feature_count},
        {"labelCount", label_count},
        {"labelBreakdown", count_label_breakdown(labels_path)},
        {"hasExport", feature_count > 0 && label_count > 0},
        {"modelOnnxExists", std::filesystem::is_regular_file(out_dir / "model.onnx")},
        {"metricsExists", std::filesystem::is_regular_file(metrics_path)},
        {"metrics", metrics.value_or(nlohmann::json(nullptr))},
        {"pythonAvailable", find_python().has_value()},
        {"repoPath", repo_path ? nlohmann::json(repo_path->string()) : nlohmann::json(nullptr)},
        {"repoConfigured", repo_path.has_value()},
        {"pipelineCommand", build_pipeline_command(out_dir)},
    };
}

nlohmann::json train_from_export(const std::filesystem::path& app_data_dir) {
    const auto status = training_deploy_status(app_data_dir);
    if (!status.value("hasExport", false)) {
        throw std::runtime_error(
            "Export training data first (need features.csv and labels.csv in your export folder).");
    }

    const auto repo_path = read_training_repo_path(app_data_dir);
    if (!repo_path) {
        throw std::runtime_error(
            "Snapback repo path not set. Enter your repo folder below or set SNAPBACK_REPO.");
    }

    const auto python = find_python();
    if (!python) {
        throw std::runtime_error(
            "Python not found. Install Python 3 and: pip install xgboost onnxmltools onnx");
    }

    const auto out_dir = export_dir(app_data_dir);
    const auto log_path = out_dir / "training.log";
    std::filesystem::create_directories(out_dir);

    std::ostringstream cmd;
#if defined(_WIN32)
    cmd << "cd /d " << quote(*repo_path) << " && " << python->program;
#else
    cmd << "cd " << quote(*repo_path) << " && " << python->program;
#endif
    for (const auto& arg : python->prefix_args) cmd << ' ' << quote_arg(arg);
    cmd << " -m ml.pipeline_cli --output-dir " << quote(out_dir)
        << " --skip-export > " << quote(log_path) << " 2>&1";

    const int exit_code = detail::normalized_exit_code(std::system(cmd.str().c_str()));
    const std::string log_tail = read_file_tail(log_path, 12);
    const bool onnx_exported = std::filesystem::is_regular_file(out_dir / "model.onnx");
    const auto metrics = parse_metrics_json(out_dir / "metrics.json");
    const bool training_succeeded = exit_code == 0;
    std::optional<std::string> sync_warning;
    if (training_succeeded && onnx_exported) {
        try {
            sync_trained_model_to_app_dir(app_data_dir, out_dir);
        } catch (const std::exception& err) {
            sync_warning = err.what();
        }
    }

    std::string message;
    if (!training_succeeded) {
        message = build_failure_message(exit_code, log_tail);
    } else if (onnx_exported) {
        message = "Training complete - model.onnx is ready. Reload model to activate.";
    } else {
        message = "Training finished but ONNX export was skipped (majority stub or missing export "
                  "deps).";
    }
    if (sync_warning) message += " Warning: " + *sync_warning;

    return nlohmann::json{
        {"success", training_succeeded && onnx_exported},
        {"trainingSucceeded", training_succeeded},
        {"deployReady", training_succeeded && onnx_exported},
        {"message", message},
        {"onnxExported", onnx_exported},
        {"metrics", metrics.value_or(nlohmann::json(nullptr))},
        {"logTail", log_tail},
    };
}

}  // namespace snapback::training_deploy
