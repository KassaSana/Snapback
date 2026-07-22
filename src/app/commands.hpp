// The webview IPC bridge. Rust: commands.rs (#[tauri::command] fns) + events.rs.
//
// Tauri auto-generates the JS invoke() glue from #[tauri::command]. webview/webview has
// no codegen, so we register each command by hand with webview.bind(name, handler): the
// handler receives the call arguments as a JSON *array* string, we take element [0] (the
// args object the shim forwarded), call AppState, and return JSON. The command names
// here must match the frontend's invoke() calls AND the Rust generate_handler![...] list
// — that three-way match is the contract (see frontend/README.md).
#pragma once

#include "app/webview_compat.hpp"  // webview.h + X11 macro scrub — never include webview.h raw

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "app/autostart.hpp"
#include "app/command_dispatch.hpp"  // pure, webview-free dispatch + validation
#include "app/state.hpp"
#include "app/training_deploy.hpp"
#include "snapback/overlay.hpp"

namespace snapback {
namespace detail {

// Wraps a JSON-in/JSON-out handler as a webview sync binding. The unwrap/serialize/
// error-envelope contract lives in run_json_command (command_dispatch.hpp) so it can be
// unit-tested without webview.
inline void bind_cmd(webview::webview& w, const std::string& name, JsonHandler handler) {
    w.bind(name, [handler = std::move(handler)](const std::string& req) -> std::string {
        return run_json_command(handler, req);
    });
}

}  // namespace detail

// Registers every command the frontend calls. One bind() per Rust #[tauri::command].
// `data_dir` is where training exports are written (Rust derives it from app_data_dir).
inline void register_commands(webview::webview& w, AppState& state,
                              const std::filesystem::path& data_dir) {
    using detail::bind_cmd;
    using nlohmann::json;

    // --- Health + predictions ---
    bind_cmd(w, "get_health", [&state](const json&) { return json(state.health()); });
    bind_cmd(w, "get_diagnostics", [&state](const json&) { return json(state.diagnostics()); });
    bind_cmd(w, "get_latest_prediction", [&state](const json&) {
        auto p = state.latest_prediction();
        return p ? json(*p) : json(nullptr);
    });
    bind_cmd(w, "get_prediction_history", [&state](const json& a) {
        return json(state.prediction_history(detail::clamp_limit(a, 8)));
    });
    bind_cmd(w, "get_focus_summary", [&state](const json& a) {
        return json(state.focus_summary(detail::clamp_limit(a, 200)));
    });

    // --- Session lifecycle ---
    bind_cmd(w, "start_session", [&state](const json& a) {
        auto goal = detail::validate_required_text("Session goal", a.at("goal").get<std::string>(),
                                                   detail::kMaxSessionGoalLen);
        auto mode = focus_mode_from_string(a.value("focusMode", std::string("normal")));
        return json(state.start_session(goal, mode));
    });
    bind_cmd(w, "stop_session", [&state](const json& a) {
        return json(state.stop_session(a.at("sessionId").get<std::string>()));
    });
    bind_cmd(w, "get_session", [&state](const json& a) {
        auto s = state.get_session(a.at("sessionId").get<std::string>());
        if (!s) throw std::runtime_error("session not found");
        return json(*s);
    });
    bind_cmd(w, "get_active_session", [&state](const json&) {
        auto s = state.active_session();
        return s ? json(*s) : json(nullptr);
    });
    bind_cmd(w, "get_session_recap", [&state](const json& a) {
        return json(state.session_recap(a.at("sessionId").get<std::string>()));
    });
    bind_cmd(w, "get_session_history", [&state](const json& a) {
        return json(state.session_history(detail::clamp_limit(a, 20)));
    });

    // --- Optional Pomodoro timer ---
    bind_cmd(w, "get_pomodoro_status",
             [&state](const json&) { return json(state.pomodoro_status()); });
    bind_cmd(w, "start_pomodoro",
             [&state](const json&) { return json(state.start_pomodoro()); });
    bind_cmd(w, "stop_pomodoro",
             [&state](const json&) { return json(state.stop_pomodoro()); });

    // --- Feedback + config ---
    bind_cmd(w, "submit_label", [&state](const json& a) {
        auto req = a.at("request").get<LabelRequest>();
        auto sid = detail::validate_required_text("Session ID", req.session_id,
                                                  detail::kMaxSessionIdLen);
        auto notes = detail::validate_optional_text("Label notes", req.notes,
                                                    detail::kMaxLabelNotesLen);
        auto source = std::string(label_source_as_str(label_source_parse(req.source)));
        state.submit_label(sid, req.label, source, notes);
        return json(nullptr);
    });
    bind_cmd(w, "set_focus_mode", [&state](const json& a) {
        state.set_focus_mode(focus_mode_from_string(a.at("mode").get<std::string>()));
        return json(nullptr);
    });
    bind_cmd(w, "get_settings", [&state](const json&) { return json(state.settings()); });
    bind_cmd(w, "get_privacy_settings", [&state](const json&) {
        return json(state.privacy_settings());
    });
    bind_cmd(w, "get_analytics", [&state](const json&) { return json(state.analytics()); });
    bind_cmd(w, "get_summary_report", [&state](const json& a) {
        return json(state.summary_report(a.value("window", std::string("day"))));
    });
    bind_cmd(w, "export_summary_report", [&state, data_dir](const json& a) {
        return json(state.export_summary_report(data_dir / "exports" / "summaries",
                                                 a.value("window", std::string("day"))));
    });
    bind_cmd(w, "set_private_mode", [&state](const json& a) {
        state.set_private_mode(a.at("enabled").get<bool>());
        return json(state.privacy_settings());
    });
    bind_cmd(w, "set_privacy_exclusions", [&state](const json& a) {
        auto exclusions = a.at("excludedApps").get<std::vector<std::string>>();
        if (exclusions.size() > 50) throw std::runtime_error("too many privacy exclusions");
        for (const auto& exclusion : exclusions) {
            if (exclusion.size() > 120) throw std::runtime_error("privacy exclusion is too long");
        }
        state.set_privacy_exclusions(std::move(exclusions));
        return json(state.privacy_settings());
    });
    bind_cmd(w, "get_goal_categories", [&state](const json&) {
        return json(state.goal_categories());
    });
    bind_cmd(w, "set_goal_categories", [&state](const json& a) {
        auto categories = a.at("categories").get<std::vector<GoalCategory>>();
        if (categories.size() > 20) throw std::runtime_error("too many goal categories");
        for (const auto& category : categories) {
            if (category.name.size() > 80 || category.keywords.size() > 50) {
                throw std::runtime_error("goal category is too large");
            }
        }
        state.set_goal_categories(std::move(categories));
        return json(state.goal_categories());
    });
    bind_cmd(w, "get_autostart", [](const json&) {
        return json{{"enabled", autostart_enabled()}, {"supported", autostart_supported()}};
    });
    bind_cmd(w, "set_autostart", [](const json& a) {
        const bool enabled = a.at("enabled").get<bool>();
        if (!autostart_supported()) {
            throw std::runtime_error("autostart is not supported on this platform");
        }
        if (!set_autostart_enabled(enabled)) {
            throw std::runtime_error("could not update autostart setting");
        }
        return json{{"enabled", autostart_enabled()}, {"supported", true}};
    });
    bind_cmd(w, "dismiss_snapback", [&state](const json&) {
        state.dismiss_snapback();
        // Bind callbacks run on the UI thread, so hiding the native overlay is safe here.
        Overlay::instance().dismiss();
        return json(nullptr);
    });

    // --- App rules (allow/block overrides) ---
    bind_cmd(w, "get_app_rules", [&state](const json&) { return json(state.app_rules()); });
    bind_cmd(w, "upsert_app_rule", [&state](const json& a) {
        auto req = a.at("request").get<UpsertAppRuleRequest>();
        auto pattern = detail::validate_required_text("App rule pattern", req.pattern,
                                                      detail::kMaxAppRulePatternLen);
        auto note = detail::validate_optional_text("App rule note", req.note,
                                                   detail::kMaxAppRuleNoteLen);
        return json(state.upsert_app_rule(pattern, req.rule_type, note));
    });
    bind_cmd(w, "delete_app_rule", [&state](const json& a) {
        state.delete_app_rule(a.at("id").get<std::int64_t>());
        return json(nullptr);
    });

    // --- Context timeline ---
    bind_cmd(w, "get_context_timeline", [&state](const json& a) {
        return json(state.context_timeline(detail::opt_string(a, "sessionId"),
                                           detail::clamp_limit(a, 20)));
    });

    // --- ONNX + permissions ---
    bind_cmd(w, "reload_classifier_model",
             [&state](const json&) { return json(state.classifier_status()); });
    bind_cmd(w, "refresh_permissions",
             [&state](const json&) { return json(state.refresh_permissions()); });
    // User-initiated only (wizard "Grant access" button) — this one can raise an OS dialog,
    // so it must never be called from a poll the way refresh_permissions is.
    bind_cmd(w, "request_permissions",
             [&state](const json&) { return json(state.request_permissions()); });

    // --- Training data export ---
    bind_cmd(w, "export_training_data", [&state, data_dir](const json& a) {
        const auto out_dir = data_dir / "exports" / "training";
        return json(state.export_training_data(out_dir, detail::opt_string(a, "sessionId")));
    });

    // --- Training pipeline ---
    bind_cmd(w, "get_training_deploy_status", [data_dir](const json&) {
        return training_deploy::training_deploy_status(data_dir);
    });
    bind_cmd(w, "set_training_repo_path", [data_dir](const json& a) {
        auto repo_path = detail::validate_required_text(
            "Repo path", a.at("repoPath").get<std::string>(), detail::kMaxRepoPathLen);
        training_deploy::write_training_repo_path(data_dir, repo_path);
        return json(nullptr);
    });
    bind_cmd(w, "train_from_export", [data_dir](const json&) {
        return training_deploy::train_from_export(data_dir);
    });
}

// Rust: events::emit_or_log — push an event to the frontend. Tauri has app.emit(); here
// we eval() a JS call the shim listens on (window.__snapback.emit). MUST run on the UI
// thread (see AppState's emit hook, which marshals via webview.dispatch).
inline void emit(webview::webview& w, const char* event, const std::string& json_payload) {
    w.eval("window.__snapback && window.__snapback.emit(\"" + std::string(event) +
           "\", " + json_payload + ")");
}

}  // namespace snapback
