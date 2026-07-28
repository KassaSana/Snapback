#include "app/training_deploy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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

#include "engine/onnx_model.hpp"

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

std::optional<nlohmann::json> parse_json_object(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    if (!parsed.is_object()) return std::nullopt;
    return parsed;
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

struct DeploymentPaths {
    explicit DeploymentPaths(const std::filesystem::path& root)
        : deployed_model(root / "model.onnx"),
          deployed_quality(root / "model_quality.json"),
          previous_model(root / "model.onnx.previous"),
          previous_quality(root / "model_quality.json.previous"),
          staged_model(root / "model.onnx.deploying"),
          staged_quality(root / "model_quality.json.deploying"),
          backup_model(root / "model.onnx.deploy-backup"),
          backup_quality(root / "model_quality.json.deploy-backup"),
          previous_model_backup(root / "model.onnx.previous.deploy-backup"),
          previous_quality_backup(root / "model_quality.json.previous.deploy-backup"),
          marker(root / "model_deploy.transaction.json"),
          committed(root / "model_deploy.transaction.committed") {}

    std::filesystem::path deployed_model;
    std::filesystem::path deployed_quality;
    std::filesystem::path previous_model;
    std::filesystem::path previous_quality;
    std::filesystem::path staged_model;
    std::filesystem::path staged_quality;
    std::filesystem::path backup_model;
    std::filesystem::path backup_quality;
    std::filesystem::path previous_model_backup;
    std::filesystem::path previous_quality_backup;
    std::filesystem::path marker;
    std::filesystem::path committed;
};

void write_file_checked(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("could not write " + path.filename().string());
    out << contents;
    out.flush();
    if (!out) throw std::runtime_error("could not finish writing " + path.filename().string());
}

void cleanup_deployment_files(const DeploymentPaths& paths) {
    std::error_code ignored;
    for (const auto& path : {paths.staged_model, paths.staged_quality, paths.backup_model,
                             paths.backup_quality, paths.previous_model_backup,
                             paths.previous_quality_backup, paths.committed, paths.marker}) {
        std::filesystem::remove(path, ignored);
    }
}

void recover_model_deployment_impl(const std::filesystem::path& app_data_dir) {
    const DeploymentPaths paths(app_data_dir);
    if (!std::filesystem::is_regular_file(paths.marker)) {
        // The marker is removed last on successful cleanup. A sentinel without it can only
        // be stale cleanup debris and must not bless a future transaction as committed.
        std::error_code ignored;
        std::filesystem::remove(paths.committed, ignored);
        return;
    }

    const auto state = parse_json_object(paths.marker);
    if (!state) {
        throw std::runtime_error("model deployment recovery marker is invalid");
    }
    if (std::filesystem::is_regular_file(paths.committed)) {
        cleanup_deployment_files(paths);
        return;
    }

    const bool had_model = state->value("hadModel", false);
    const bool had_quality = state->value("hadQuality", false);
    const bool had_previous_model = state->value("hadPreviousModel", false);
    const bool had_previous_quality = state->value("hadPreviousQuality", false);

    auto restore = [](const std::filesystem::path& destination,
                      const std::filesystem::path& backup, bool existed) {
        if (std::filesystem::is_regular_file(backup)) {
            std::error_code ignored;
            std::filesystem::remove(destination, ignored);
            std::filesystem::rename(backup, destination);
        } else if (!existed) {
            std::error_code ignored;
            std::filesystem::remove(destination, ignored);
        }
        // If `existed` is true and no backup exists, the transaction had not moved the
        // original yet; leave the original destination untouched.
    };

    restore(paths.deployed_model, paths.backup_model, had_model);
    restore(paths.deployed_quality, paths.backup_quality, had_quality);
    restore(paths.previous_model, paths.previous_model_backup, had_previous_model);
    restore(paths.previous_quality, paths.previous_quality_backup, had_previous_quality);
    cleanup_deployment_files(paths);
}

bool sync_trained_model_to_app_dir(const std::filesystem::path& app_data_dir,
                                   const std::filesystem::path& candidate_model,
                                   const nlohmann::json& quality_metadata) {
    if (!std::filesystem::is_regular_file(candidate_model)) return false;
    std::filesystem::create_directories(app_data_dir);
    recover_model_deployment_impl(app_data_dir);
    const DeploymentPaths paths(app_data_dir);

    // Stage and validate both artifacts before touching the live pair. In particular,
    // metadata write failure must not leave a new model with the old accepted baseline.
    std::filesystem::copy_file(candidate_model, paths.staged_model);
    const auto identity = OnnxModel::model_id_for_path(paths.staged_model);
    if (!identity) {
        std::filesystem::remove(paths.staged_model);
        return false;
    }
    {
        std::ofstream quality_file(paths.staged_quality, std::ios::trunc);
        if (!quality_file) {
            std::filesystem::remove(paths.staged_model);
            return false;
        }
        auto metadata = quality_metadata;
        metadata["modelId"] = *identity;
        quality_file << metadata.dump(2);
        quality_file.flush();
        if (!quality_file) {
            std::filesystem::remove(paths.staged_model);
            std::filesystem::remove(paths.staged_quality);
            return false;
        }
    }

    const bool had_model = std::filesystem::is_regular_file(paths.deployed_model);
    const bool had_quality = std::filesystem::is_regular_file(paths.deployed_quality);
    const bool had_previous_model = std::filesystem::is_regular_file(paths.previous_model);
    const bool had_previous_quality = std::filesystem::is_regular_file(paths.previous_quality);
    try {
        write_file_checked(
            paths.marker,
            nlohmann::json{{"hadModel", had_model},
                           {"hadQuality", had_quality},
                           {"hadPreviousModel", had_previous_model},
                           {"hadPreviousQuality", had_previous_quality}}
                .dump());

        // Preserve the accepted pair as the user-visible rollback target before promotion.
        if (had_model) {
            if (had_previous_model) {
                std::filesystem::rename(paths.previous_model, paths.previous_model_backup);
            }
            if (had_previous_quality) {
                std::filesystem::rename(paths.previous_quality,
                                        paths.previous_quality_backup);
            }
            std::filesystem::copy_file(paths.deployed_model, paths.previous_model);
            if (had_quality) {
                std::filesystem::copy_file(paths.deployed_quality, paths.previous_quality);
            }
        }

        if (had_model) std::filesystem::rename(paths.deployed_model, paths.backup_model);
        if (had_quality) {
            std::filesystem::rename(paths.deployed_quality, paths.backup_quality);
        }
        std::filesystem::rename(paths.staged_model, paths.deployed_model);
        std::filesystem::rename(paths.staged_quality, paths.deployed_quality);
        write_file_checked(paths.committed, "committed\n");
    } catch (...) {
        if (std::filesystem::is_regular_file(paths.marker)) {
            recover_model_deployment_impl(app_data_dir);
        } else {
            cleanup_deployment_files(paths);
        }
        throw;
    }
    // The commit sentinel makes cleanup crash-safe: recovery keeps the new pair if cleanup
    // was interrupted after the promotion committed.
    cleanup_deployment_files(paths);
    return true;
}

void swap_file(const std::filesystem::path& first, const std::filesystem::path& second) {
    const auto temporary = first.string() + ".rollback-temp";
    const std::filesystem::path temp_path(temporary);
    std::filesystem::copy_file(first, temp_path,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(second, first,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(temp_path, second,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(temp_path);
}

void swap_optional_file(const std::filesystem::path& first,
                        const std::filesystem::path& second) {
    const bool first_exists = std::filesystem::is_regular_file(first);
    const bool second_exists = std::filesystem::is_regular_file(second);
    if (first_exists && second_exists) {
        swap_file(first, second);
    } else if (second_exists) {
        std::filesystem::copy_file(second, first,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(second);
    } else if (first_exists) {
        std::filesystem::remove(first);
    }
}

}  // namespace

void recover_model_deployment(const std::filesystem::path& app_data_dir) {
    recover_model_deployment_impl(app_data_dir);
}

bool deploy_model_candidate(const std::filesystem::path& app_data_dir,
                            const std::filesystem::path& candidate_model,
                            const ModelQualityDecision& quality) {
    if (!quality.accepted || quality.metric.empty()) return false;
    return sync_trained_model_to_app_dir(
        app_data_dir, candidate_model,
        nlohmann::json{{"metric", quality.metric}, {"score", quality.candidate_score}});
}

ModelQualityDecision evaluate_model_quality(
    const nlohmann::json& candidate_metrics,
    const std::optional<nlohmann::json>& deployed_quality) {
    constexpr double kMinimumAccuracy = 0.60;
    constexpr const char* kMetricNames[] = {
        "held_out_accuracy", "validation_accuracy", "cv_accuracy"};

    ModelQualityDecision decision;
    for (const char* name : kMetricNames) {
        if (candidate_metrics.contains(name) && candidate_metrics.at(name).is_number()) {
            decision.metric = name;
            decision.candidate_score = candidate_metrics.at(name).get<double>();
            break;
        }
    }

    if (decision.metric.empty() || !std::isfinite(decision.candidate_score) ||
        decision.candidate_score < 0.0 || decision.candidate_score > 1.0) {
        decision.reason =
            "Model rejected: metrics.json must contain a held-out accuracy between 0 and 1.";
        return decision;
    }

    decision.threshold = kMinimumAccuracy;
    if (deployed_quality) {
        const auto& baseline = *deployed_quality;
        if (!baseline.contains("metric") || !baseline.at("metric").is_string() ||
            baseline.at("metric").get<std::string>() != decision.metric ||
            !baseline.contains("score") || !baseline.at("score").is_number()) {
            decision.reason = "Model rejected: deployed model quality baseline is invalid.";
            return decision;
        }
        const double baseline_score = baseline.at("score").get<double>();
        if (!std::isfinite(baseline_score) || baseline_score < 0.0 || baseline_score > 1.0) {
            decision.reason = "Model rejected: deployed model quality baseline is invalid.";
            return decision;
        }
        decision.threshold = std::max(kMinimumAccuracy, baseline_score);
    }

    decision.accepted = decision.candidate_score >= decision.threshold;
    if (decision.accepted) {
        decision.reason = "Model quality gate passed (" + decision.metric + "=" +
                          std::to_string(decision.candidate_score) + ").";
    } else {
        decision.reason = "Model rejected: " + decision.metric + "=" +
                          std::to_string(decision.candidate_score) + " is below the " +
                          std::to_string(decision.threshold) + " deployment threshold.";
    }
    return decision;
}

std::filesystem::path export_dir(const std::filesystem::path& app_data_dir) {
    return app_data_dir / "exports" / "training";
}

bool rollback_available(const std::filesystem::path& app_data_dir) {
    return std::filesystem::is_regular_file(app_data_dir / "model.onnx.previous");
}

nlohmann::json rollback_model(const std::filesystem::path& app_data_dir) {
    recover_model_deployment(app_data_dir);
    const auto current = app_data_dir / "model.onnx";
    const auto previous = app_data_dir / "model.onnx.previous";
    if (!rollback_available(app_data_dir)) {
        throw std::runtime_error("No previous model is available to restore.");
    }

    swap_optional_file(current, previous);
    swap_optional_file(app_data_dir / "model_quality.json",
                       app_data_dir / "model_quality.json.previous");
    const auto identity = OnnxModel::model_id_for_path(current);
    return nlohmann::json{{"success", true},
                          {"message", "Previous model restored. Reloading classifier."},
                          {"modelId", identity ? nlohmann::json(*identity)
                                                 : nlohmann::json(nullptr)}};
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
    const auto deployed_quality = parse_json_object(app_data_dir / "model_quality.json");
    const auto repo_path = read_training_repo_path(app_data_dir);
    const std::uint64_t feature_count = count_csv_rows(features_path);
    const std::uint64_t label_count = count_csv_rows(labels_path);

    nlohmann::json quality_gate = nlohmann::json{{"passed", false}};
    if (metrics) {
        const auto decision = evaluate_model_quality(*metrics, deployed_quality);
        quality_gate = nlohmann::json{{"passed", decision.accepted},
                                      {"metric", decision.metric},
                                      {"candidateScore", decision.candidate_score},
                                      {"threshold", decision.threshold},
                                      {"reason", decision.reason}};
    }

    return nlohmann::json{
        {"exportDir", out_dir.string()},
        {"featureCount", feature_count},
        {"labelCount", label_count},
        {"labelBreakdown", count_label_breakdown(labels_path)},
        {"hasExport", feature_count > 0 && label_count > 0},
        {"modelOnnxExists", std::filesystem::is_regular_file(out_dir / "model.onnx")},
        {"metricsExists", std::filesystem::is_regular_file(metrics_path)},
        {"metrics", metrics.value_or(nlohmann::json(nullptr))},
        {"qualityGate", quality_gate},
        {"rollbackAvailable", rollback_available(app_data_dir)},
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
    const auto deployed_quality = parse_json_object(app_data_dir / "model_quality.json");
    ModelQualityDecision quality;
    bool quality_checked = false;
    std::optional<std::string> sync_warning;
    if (training_succeeded && onnx_exported) {
        if (metrics) {
            quality = evaluate_model_quality(*metrics, deployed_quality);
            quality_checked = true;
        } else {
            quality.reason =
                "Model rejected: metrics.json is missing a held-out accuracy metric.";
            quality_checked = true;
        }
        if (quality.accepted) {
            try {
                if (!deploy_model_candidate(app_data_dir, out_dir / "model.onnx", quality)) {
                    sync_warning =
                        "could not stage model.onnx and its quality metadata";
                }
            } catch (const std::exception& err) {
                sync_warning = err.what();
            }
        }
    }

    std::string message;
    if (!training_succeeded) {
        message = build_failure_message(exit_code, log_tail);
    } else if (onnx_exported) {
        message = quality.reason;
        if (quality.accepted && !sync_warning) {
            message += " Training complete - model.onnx is ready. Reload model to activate.";
        }
    } else {
        message = "Training finished but ONNX export was skipped (majority stub or missing export "
                  "deps).";
    }
    if (sync_warning) message += " Warning: " + *sync_warning;

    return nlohmann::json{
        {"success", training_succeeded && onnx_exported && quality.accepted && !sync_warning},
        {"trainingSucceeded", training_succeeded},
        {"deployReady", training_succeeded && onnx_exported && quality.accepted && !sync_warning},
        {"message", message},
        {"onnxExported", onnx_exported},
        {"metrics", metrics.value_or(nlohmann::json(nullptr))},
        {"qualityGatePassed", quality_checked && quality.accepted},
        {"qualityGateReason", quality.reason},
        {"logTail", log_tail},
    };
}

}  // namespace snapback::training_deploy
