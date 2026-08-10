// The webview IPC bridge and native command registry.
//
// We register each command by hand with webview.bind(name, handler): the
// handler receives the call arguments as a JSON *array* string, we take element [0] (the
// args object the shim forwarded), call AppState, and return JSON. The command names
// here must match the frontend's invoke() calls — that match is the IPC contract.
#pragma once

#include "app/webview_compat.hpp"  // webview.h + X11 macro scrub — never include webview.h raw

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "app/autostart.hpp"
#include "app/command_dispatch.hpp"  // pure, webview-free dispatch + validation
#include "app/frontend_assets.hpp"
#include "app/open_url.hpp"
#include "app/reveal_path.hpp"
#include "app/state.hpp"
#include "app/support_bundle.hpp"
#include "app/training_deploy.hpp"
#include "snapback/overlay.hpp"

namespace snapback {
namespace detail {

// Wraps a JSON-in/JSON-out handler as a webview sync binding. The unwrap/serialize/
// error-envelope contract lives in run_json_command (command_dispatch.hpp) so it can be
// unit-tested without webview.
inline void bind_cmd(webview::webview& w, const std::string& name, JsonHandler handler,
                     std::string token) {
    w.bind(name, [handler = std::move(handler), token = std::move(token)](
                     const std::string& req) -> std::string {
        // Roadmap 8.14. Every command, without exception — including the read-only ones. A
        // page that can read your session history has read your window titles, so "only
        // mutations need the check" would be the wrong boundary.
        return run_json_command(handler, req, token);
    });
}

}  // namespace detail

// Registers every command the frontend calls. `data_dir` is where training exports
// are written.
inline void register_commands(webview::webview& w, AppState& state,
                              const std::filesystem::path& data_dir,
                              const std::string& capability_token = {}) {
    using nlohmann::json;
    // Roadmap 8.14. Wrapped so every `bind_cmd(...)` call below carries the token without each
    // one having to remember: a command registered without it would be a silently
    // unprotected hole in exactly the surface this exists to protect.
    const auto bind_cmd = [&w, &capability_token](const std::string& name,
                                                  detail::JsonHandler handler) {
        detail::bind_cmd(w, name, std::move(handler), capability_token);
    };

    // --- Health + predictions ---
    bind_cmd("get_health", [&state](const json&) { return json(state.health()); });
    bind_cmd("get_diagnostics", [&state](const json&) {
        auto result = json(state.diagnostics());
        result["supportBundlePrivacyNotice"] = kSupportBundlePrivacyNotice;
        return result;
    });
    bind_cmd("export_support_bundle", [&state, data_dir](const json&) {
        const auto exported =
            export_support_bundle(data_dir / "exports" / "support", state.diagnostics());
        return json{{"outputPath", exported.output_path},
                    {"privacyNotice", exported.privacy_notice}};
    });
    bind_cmd("get_latest_prediction", [&state](const json&) {
        auto p = state.latest_prediction();
        return p ? json(*p) : json(nullptr);
    });
    bind_cmd("get_prediction_history", [&state](const json& a) {
        return json(state.prediction_history(detail::clamp_limit(a, 8)));
    });
    bind_cmd("get_focus_summary", [&state](const json& a) {
        return json(state.focus_summary(detail::clamp_limit(a, 200)));
    });

    // --- Session lifecycle ---
    bind_cmd("start_session", [&state](const json& a) {
        auto goal = detail::validate_required_text("Session goal", a.at("goal").get<std::string>(),
                                                   detail::kMaxSessionGoalLen);
        auto mode = focus_mode_from_string(a.value("focusMode", std::string("normal")));
        return json(state.start_session(goal, mode));
    });
    bind_cmd("stop_session", [&state](const json& a) {
        return json(state.stop_session(a.at("sessionId").get<std::string>()));
    });
    bind_cmd("get_session", [&state](const json& a) {
        auto s = state.get_session(a.at("sessionId").get<std::string>());
        if (!s) throw std::runtime_error("session not found");
        return json(*s);
    });
    bind_cmd("get_active_session", [&state](const json&) {
        auto s = state.active_session();
        return s ? json(*s) : json(nullptr);
    });
    bind_cmd("get_session_recap", [&state](const json& a) {
        return json(state.session_recap(a.at("sessionId").get<std::string>()));
    });
    bind_cmd("get_session_history", [&state](const json& a) {
        return json(state.session_history(detail::clamp_limit(a, 20)));
    });
    // Roadmap 2.14. Both answers are optional, and blank is not an answer:
    // validate_optional_text trims and turns "" into nullopt, so Skip, an all-whitespace
    // submission, and clearing a previous answer all land on the same NULL the schema uses
    // for "never answered". Returns the saved row so the UI renders what was stored rather
    // than what it hoped was stored.
    bind_cmd("save_session_reflection", [&state](const json& a) {
        auto sid = detail::validate_required_text(
            "Session ID", a.at("sessionId").get<std::string>(), detail::kMaxSessionIdLen);
        auto done = detail::validate_optional_text("What got done",
                                                   detail::opt_string(a, "done"),
                                                   detail::kMaxReflectionLen);
        auto next_step = detail::validate_optional_text("Next step",
                                                        detail::opt_string(a, "nextStep"),
                                                        detail::kMaxReflectionLen);
        auto saved = state.save_session_reflection(sid, done, next_step);
        if (!saved) throw std::runtime_error("session not found");
        return json(*saved);
    });

    // --- Optional Pomodoro timer ---
    bind_cmd("get_pomodoro_status",
             [&state](const json&) { return json(state.pomodoro_status()); });
    bind_cmd("start_pomodoro",
             [&state](const json&) { return json(state.start_pomodoro()); });
    bind_cmd("stop_pomodoro",
             [&state](const json&) { return json(state.stop_pomodoro()); });
    // Roadmap 2.13. Each returns the resulting status, so the UI renders the state the timer
    // actually reached rather than the one the click hoped for — pause on an already-ended
    // phase, or acknowledge on a running one, are deliberate no-ops.
    bind_cmd("pause_pomodoro", [&state](const json&) { return json(state.pause_pomodoro()); });
    bind_cmd("resume_pomodoro",
             [&state](const json&) { return json(state.resume_pomodoro()); });
    bind_cmd("skip_pomodoro_phase",
             [&state](const json&) { return json(state.skip_pomodoro_phase()); });
    bind_cmd("restart_pomodoro_phase",
             [&state](const json&) { return json(state.restart_pomodoro_phase()); });
    bind_cmd("acknowledge_pomodoro_phase",
             [&state](const json&) { return json(state.acknowledge_pomodoro_phase()); });
    // Roadmap 2.10. One status, one command. The header and the tray both call this rather
    // than each deriving "am I recording?" from health plus settings, which is how two
    // surfaces come to disagree about the only question this app must never be vague on.
    bind_cmd("get_recording_status",
             [&state](const json&) { return json(state.recording_status()); });
    bind_cmd("pause_recording_privately", [&state](const json& a) {
        // 0 (or absent) means indefinite, matching the Settings toggle.
        return json(state.pause_privately_for(a.value("minutes", std::int64_t{0})));
    });
    bind_cmd("resume_recording",
             [&state](const json&) { return json(state.resume_from_private_pause()); });
    // Roadmap 2.19. Opt-in attended-minute targets. Reported together with the actuals so
    // the UI never has to pair a plan with a total fetched separately and possibly later.
    bind_cmd("get_attended_progress",
             [&state](const json&) { return json(state.attended_progress()); });
    bind_cmd("set_attended_targets", [&state](const json& a) {
        // 0 turns a target off, which is also the default -- there is no separate "enabled"
        // flag to drift out of step with the number.
        return json(state.set_attended_targets(a.value("dailyMins", 0u),
                                               a.value("weeklyMins", 0u)));
    });
    // The rhythm itself. Read it back through get_settings, which already carries it — a
    // dedicated getter would be a second description of one thing.
    bind_cmd("set_pomodoro_config", [&state](const json& a) {
        return json(state.set_pomodoro_config(a.at("config").get<PomodoroConfig>()));
    });

    // --- Feedback + config ---
    bind_cmd("submit_label", [&state](const json& a) {
        auto req = a.at("request").get<LabelRequest>();
        auto sid = detail::validate_required_text("Session ID", req.session_id,
                                                  detail::kMaxSessionIdLen);
        auto notes = detail::validate_optional_text("Label notes", req.notes,
                                                    detail::kMaxLabelNotesLen);
        auto source = std::string(label_source_as_str(label_source_parse(req.source)));
        state.submit_label(sid, req.label, source, notes);
        return json(nullptr);
    });
    bind_cmd("set_focus_mode", [&state](const json& a) {
        state.set_focus_mode(focus_mode_from_string(a.at("mode").get<std::string>()));
        return json(nullptr);
    });
    bind_cmd("get_settings", [&state](const json&) { return json(state.settings()); });
    // Roadmap 7.23. Returns the whole settings object rather than null so the UI renders the
    // value the app actually accepted, not the one it optimistically sent.
    bind_cmd("set_idle_threshold", [&state](const json& a) {
        state.set_idle_threshold_secs(a.at("seconds").get<std::int64_t>());
        return json(state.settings());
    });
    bind_cmd("get_privacy_settings", [&state](const json&) {
        return json(state.privacy_settings());
    });
    bind_cmd("get_analytics", [&state](const json&) { return json(state.analytics()); });
    bind_cmd("get_summary_report", [&state](const json& a) {
        return json(state.summary_report(a.value("window", std::string("day"))));
    });
    bind_cmd("export_summary_report", [&state, data_dir](const json& a) {
        return json(state.export_summary_report(data_dir / "exports" / "summaries",
                                                 a.value("window", std::string("day"))));
    });
    bind_cmd("set_private_mode", [&state](const json& a) {
        state.set_private_mode(a.at("enabled").get<bool>());
        return json(state.privacy_settings());
    });
    bind_cmd("set_privacy_exclusions", [&state](const json& a) {
        auto exclusions = a.at("excludedApps").get<std::vector<std::string>>();
        if (exclusions.size() > 50) throw std::runtime_error("too many privacy exclusions");
        for (const auto& exclusion : exclusions) {
            if (exclusion.size() > 120) throw std::runtime_error("privacy exclusion is too long");
        }
        state.set_privacy_exclusions(std::move(exclusions));
        return json(state.privacy_settings());
    });
    // Roadmap 8.12. Returns what was deleted, what could not be, and what was deliberately
    // kept. It used to return null, which left the UI able to say only "deleted" or "failed"
    // for an operation that can half-succeed.
    bind_cmd("delete_all_activity_data", [&state](const json&) {
        return json(state.delete_all_activity_data());
    });
    // Roadmap 7.6: "delete everything" was the only eraser available, which makes removing
    // one bad session cost the user their whole history. Reports whether a row was actually
    // removed rather than returning null, so the UI can distinguish a stale list entry from
    // a successful delete instead of silently claiming success for an id that never existed.
    bind_cmd("delete_session", [&state](const json& a) {
        auto sid = detail::validate_required_text("Session ID",
                                                  a.at("sessionId").get<std::string>(),
                                                  detail::kMaxSessionIdLen);
        return json(state.delete_session(sid));
    });
    // Roadmap 7.6: "local-only" is a claim the user cannot check without being able to reach
    // the files. `path` comes back whether or not the file manager opened, because "we could
    // not open it, here is where it is" is still an answer the user can act on — and on a
    // platform with no backend it is the *only* one.
    bind_cmd("open_data_folder", [data_dir](const json&) {
        return json{{"path", data_dir.string()},
                    {"supported", reveal_supported()},
                    {"opened", reveal_directory(data_dir)}};
    });
    // Roadmap 8.14. External links leave through the OS browser; this is the native half of
    // the shim's click interceptor. Only http/https/mailto are accepted.
    bind_cmd("open_external_url", [](const json& a) {
        auto url =
            detail::validate_required_text("URL", a.at("url").get<std::string>(), 4096);
        if (!detail::open_allowed_external_url(url)) {
            throw std::runtime_error("URL is not allowed to open externally");
        }
        return json{{"supported", open_external_url_supported()},
                    {"opened", open_external_url(url)}};
    });
    bind_cmd("get_goal_categories", [&state](const json&) {
        return json(state.goal_categories());
    });
    bind_cmd("set_goal_categories", [&state](const json& a) {
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
    bind_cmd("get_autostart", [](const json&) {
        return json{{"enabled", autostart_enabled()}, {"supported", autostart_supported()}};
    });
    bind_cmd("set_autostart", [](const json& a) {
        const bool enabled = a.at("enabled").get<bool>();
        if (!autostart_supported()) {
            throw std::runtime_error("autostart is not supported on this platform");
        }
        if (!set_autostart_enabled(enabled)) {
            throw std::runtime_error("could not update autostart setting");
        }
        return json{{"enabled", autostart_enabled()}, {"supported", true}};
    });
    bind_cmd("dismiss_snapback", [&state](const json&) {
        state.dismiss_snapback();
        // Bind callbacks run on the UI thread, so hiding the native overlay is safe here.
        Overlay::instance().dismiss();
        return json(nullptr);
    });

    // --- App rules (allow/block overrides) ---
    bind_cmd("get_app_rules", [&state](const json&) { return json(state.app_rules()); });
    bind_cmd("upsert_app_rule", [&state](const json& a) {
        auto req = a.at("request").get<UpsertAppRuleRequest>();
        auto pattern = detail::validate_required_text("App rule pattern", req.pattern,
                                                      detail::kMaxAppRulePatternLen);
        auto note = detail::validate_optional_text("App rule note", req.note,
                                                   detail::kMaxAppRuleNoteLen);
        return json(state.upsert_app_rule(pattern, req.rule_type, note));
    });
    bind_cmd("delete_app_rule", [&state](const json& a) {
        state.delete_app_rule(a.at("id").get<std::int64_t>());
        return json(nullptr);
    });

    // --- Context timeline ---
    bind_cmd("get_context_timeline", [&state](const json& a) {
        return json(state.context_timeline(detail::opt_string(a, "sessionId"),
                                           detail::clamp_limit(a, 20)));
    });

    // --- ONNX + permissions ---
    bind_cmd("reload_classifier_model",
             [&state](const json&) { return json(state.reload_classifier_model()); });
    bind_cmd("refresh_permissions",
             [&state](const json&) { return json(state.refresh_permissions()); });
    // User-initiated only (wizard "Grant access" button) — this one can raise an OS dialog,
    // so it must never be called from a poll the way refresh_permissions is.
    bind_cmd("request_permissions",
             [&state](const json&) { return json(state.request_permissions()); });

    // --- Training data export ---
    bind_cmd("export_training_data", [&state, data_dir](const json& a) {
        const auto out_dir = data_dir / "exports" / "training";
        return json(state.export_training_data(out_dir, detail::opt_string(a, "sessionId")));
    });

    // Roadmap 7.6: the legible counterpart to export_training_data. Separate command and
    // separate directory because they answer different questions and have different audiences
    // — one is for a training script, this one is for the person being recorded.
    bind_cmd("export_my_data", [&state, data_dir](const json&) {
        const auto result = state.export_personal_data(data_dir / "exports" / "personal");
        // Roadmap 9.16. Per-record-type omission counts and the body checksum travel with the
        // path. `truncated` is derived from the counts rather than being its own field, so the
        // old failure -- reporting a complete export after dropping windows from an included
        // session -- cannot be expressed on the wire either.
        return json{{"outputPath", result.output_path},
                    {"sessionCount", result.session_count},
                    {"windowCount", result.window_count},
                    {"episodeCount", result.episode_count},
                    {"omittedSessions", result.omitted_sessions},
                    {"omittedWindows", result.omitted_windows},
                    {"checksum", result.checksum},
                    {"truncated", result.truncated()}};
    });

    // --- Training pipeline ---
    bind_cmd("get_training_deploy_status", [data_dir](const json&) {
        if (!developer_tools_enabled()) {
            throw std::runtime_error(
                "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
        }
        return training_deploy::training_deploy_status(data_dir);
    });
    bind_cmd("set_training_repo_path", [data_dir](const json& a) {
        if (!developer_tools_enabled()) {
            throw std::runtime_error(
                "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
        }
        auto repo_path = detail::validate_required_text(
            "Repo path", a.at("repoPath").get<std::string>(), detail::kMaxRepoPathLen);
        training_deploy::write_training_repo_path(data_dir, repo_path);
        return json(nullptr);
    });
    bind_cmd("train_from_export", [data_dir](const json&) {
        if (!developer_tools_enabled()) {
            throw std::runtime_error(
                "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
        }
        return training_deploy::train_from_export(data_dir);
    });
    bind_cmd("rollback_classifier_model", [&state, data_dir](const json&) {
        auto result = training_deploy::rollback_model(data_dir);
        result["classifier"] = state.reload_classifier_model();
        return result;
    });
    // Roadmap 13.8. Retries startup-safe deployment cleanup without restarting the app.
    bind_cmd("retry_model_deployment_cleanup", [&state](const json&) {
        return json(state.retry_model_deployment_cleanup());
    });
}

// Push an event to the frontend. The shim listens on window.__snapback.emit. MUST run
// on the UI
// thread (see AppState's emit hook, which marshals via webview.dispatch).
inline void emit(webview::webview& w, const char* event, const std::string& json_payload) {
    w.eval(detail::event_dispatch_script(event, json_payload));
}

}  // namespace snapback
