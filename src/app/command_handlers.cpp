#include "app/command_handlers.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/autostart.hpp"
#include "app/async_command_runner.hpp"
#include "app/command_dispatch.hpp"
#include "app/data_import.hpp"
#include "app/file_dialog.hpp"
#include "app/frontend_assets.hpp"
#include "app/open_url.hpp"
#include "app/reveal_path.hpp"
#include "app/state.hpp"
#include "app/support_bundle.hpp"
#include "app/training_deploy.hpp"

namespace snapback {

void register_command_handlers(CommandRegistry& registry, AppState& state,
                               const std::filesystem::path& data_dir,
                               detail::AsyncCommandRunner& async_commands,
                               NativeUiHooks ui) {
    using nlohmann::json;
    // Shared by value between the two handlers below; a missing hook is a no-op, which is
    // what the headless tests and the stub platforms want.
    const auto dismiss_overlay = std::make_shared<std::function<void()>>(
        ui.dismiss_overlay ? std::move(ui.dismiss_overlay) : [] {});
    const auto report_acceptance_verdict =
        std::make_shared<std::function<void(const json&)>>(
            std::move(ui.report_acceptance_verdict));
    // Training consumes the export's files and privacy deletion erases them, so neither may
    // overlap a partially written pair. Export and training share the worker, which already
    // serialises them; the gate is what the UI-thread deletion reads.
    const auto training_export_active = std::make_shared<std::atomic<bool>>(false);
    // Held for the length of a Python run, which reads the export's CSVs and writes its model
    // and log into the same directory. Export cannot overlap it (same worker); the UI-thread
    // deletion can, and reads this to refuse.
    const auto training_active = std::make_shared<std::atomic<bool>>(false);
    // Raised by cancel_training, read by the run at each poll, cleared when the next run
    // claims the gate. Set only while the gate is held, so a click after a run has ended
    // cannot cancel the run after that.
    const auto training_cancel_requested = std::make_shared<std::atomic<bool>>(false);
    // Personal exports write one archive; two at once would race on the same path.
    const auto personal_export_active = std::make_shared<std::atomic<bool>>(false);
    // The three below were synchronous bindings until 2026-09-16, which put a full database
    // copy (VACUUM INTO) and two file writes on the webview's thread. Each has the same
    // shape as the exports: a worker job, one gate, and a UI-thread command that must not
    // touch the same files while the gate is held.
    const auto summary_export_active = std::make_shared<std::atomic<bool>>(false);
    const auto support_export_active = std::make_shared<std::atomic<bool>>(false);
    const auto import_staging_active = std::make_shared<std::atomic<bool>>(false);

    // --- Health + predictions ---
    registry.add("get_health", [&state](const json&) { return json(state.health()); });
    registry.add("report_acceptance_verdict", [report_acceptance_verdict](const json& a) {
        if (!*report_acceptance_verdict) {
            throw std::runtime_error("desktop acceptance harness is disabled in this build");
        }
        const auto& verdict = a.at("verdict");
        (*report_acceptance_verdict)(verdict);
        return json{{"accepted", true}};
    });
    registry.add("get_diagnostics", [&state](const json&) {
        auto result = json(state.diagnostics());
        result["supportBundlePrivacyNotice"] = kSupportBundlePrivacyNotice;
        return result;
    });
    registry.add_async(
        "export_support_bundle",
        [&state, data_dir](const json&) {
            const auto exported =
                export_support_bundle(data_dir / "exports" / "support", state.diagnostics());
            return json{{"outputPath", exported.output_path},
                        {"privacyNotice", exported.privacy_notice}};
        },
        support_export_active, "support bundle export is already in progress");
    registry.add("get_latest_prediction", [&state](const json&) {
        auto p = state.latest_prediction();
        return p ? json(*p) : json(nullptr);
    });
    registry.add("get_prediction_history", [&state](const json& a) {
        return json(state.prediction_history(detail::clamp_limit(a, 8)));
    });
    registry.add("get_focus_summary", [&state](const json& a) {
        if (a.contains("window")) {
            return json(state.focus_summary_for_window(a.at("window").get<std::string>(),
                                                        detail::opt_string(a, "since")));
        }
        return json(state.focus_summary(detail::clamp_limit(a, 200)));
    });

    // --- Session lifecycle ---
    registry.add("start_session", [&state](const json& a) {
        auto goal = detail::validate_required_text("Session goal", a.at("goal").get<std::string>(),
                                                   detail::kMaxSessionGoalLen);
        auto mode = focus_mode_from_string(a.value("focusMode", std::string("normal")));
        return json(state.start_session(goal, mode));
    });
    registry.add("stop_session", [&state](const json& a) {
        return json(state.stop_session(a.at("sessionId").get<std::string>()));
    });
    registry.add("get_session", [&state](const json& a) {
        auto s = state.get_session(a.at("sessionId").get<std::string>());
        if (!s) throw std::runtime_error("session not found");
        return json(*s);
    });
    registry.add("get_active_session", [&state](const json&) {
        auto s = state.active_session();
        return s ? json(*s) : json(nullptr);
    });
    registry.add("get_session_recap", [&state](const json& a) {
        return json(state.session_recap(a.at("sessionId").get<std::string>()));
    });
    registry.add("get_session_history", [&state](const json& a) {
        if (a.contains("window")) {
            return json(state.session_history_for_window(a.at("window").get<std::string>(),
                                                         detail::opt_string(a, "since")));
        }
        return json(state.session_history(detail::clamp_limit(a, 20)));
    });
    // Roadmap 2.14. Both answers are optional, and blank is not an answer:
    // validate_optional_text trims and turns "" into nullopt, so Skip, an all-whitespace
    // submission, and clearing a previous answer all land on the same NULL the schema uses
    // for "never answered". Returns the saved row so the UI renders what was stored rather
    // than what it hoped was stored.
    registry.add("save_session_reflection", [&state](const json& a) {
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
    registry.add("get_pomodoro_status",
             [&state](const json&) { return json(state.pomodoro_status()); });
    registry.add("start_pomodoro",
             [&state](const json&) { return json(state.start_pomodoro()); });
    registry.add("stop_pomodoro",
             [&state](const json&) { return json(state.stop_pomodoro()); });
    // Roadmap 2.13. Each returns the resulting status, so the UI renders the state the timer
    // actually reached rather than the one the click hoped for — pause on an already-ended
    // phase, or acknowledge on a running one, are deliberate no-ops.
    registry.add("pause_pomodoro", [&state](const json&) { return json(state.pause_pomodoro()); });
    registry.add("resume_pomodoro",
             [&state](const json&) { return json(state.resume_pomodoro()); });
    registry.add("skip_pomodoro_phase",
             [&state](const json&) { return json(state.skip_pomodoro_phase()); });
    registry.add("restart_pomodoro_phase",
             [&state](const json&) { return json(state.restart_pomodoro_phase()); });
    registry.add("acknowledge_pomodoro_phase",
             [&state](const json&) { return json(state.acknowledge_pomodoro_phase()); });
    // Roadmap 2.10. One status, one command. The header and the tray both call this rather
    // than each deriving "am I recording?" from health plus settings, which is how two
    // surfaces come to disagree about the only question this app must never be vague on.
    registry.add("get_recording_status",
             [&state](const json&) { return json(state.recording_status()); });
    registry.add("pause_recording_privately", [&state](const json& a) {
        // 0 (or absent) means indefinite, matching the Settings toggle.
        return json(state.pause_privately_for(a.value("minutes", std::int64_t{0})));
    });
    registry.add("resume_recording",
             [&state](const json&) { return json(state.resume_from_private_pause()); });
    // Roadmap 2.16. Deliberately a sibling of the two above rather than a mode of them: this
    // silences *delivery* and leaves recording running, and the returned status says both --
    // `state` still reads Recording while `alertSnoozeRemainingMs` counts down.
    registry.add("snooze_alerts", [&state](const json& a) {
        // 0 (or absent) means the default 30 minutes, which is what the tray action sends.
        return json(state.snooze_alerts_for(a.value("minutes", std::int64_t{0})));
    });
    registry.add("resume_alerts", [&state](const json&) { return json(state.resume_alerts()); });
    registry.add("set_alert_delivery", [&state](const json& a) {
        return json(state.set_alert_delivery(a.at("alerts").get<AlertDeliverySettings>()));
    });
    registry.add("dismiss_untracked_nudge", [&state](const json& a) {
        state.dismiss_untracked_nudge(a.value("minutes", std::int64_t{60}));
        return json{{"dismissed", true}};
    });
    // Roadmap 2.19. Opt-in attended-minute targets. Reported together with the actuals so
    // the UI never has to pair a plan with a total fetched separately and possibly later.
    registry.add("get_attended_progress",
             [&state](const json&) { return json(state.attended_progress()); });
    registry.add("set_attended_targets", [&state](const json& a) {
        // 0 turns a target off, which is also the default -- there is no separate "enabled"
        // flag to drift out of step with the number.
        return json(state.set_attended_targets(a.value("dailyMins", 0u),
                                               a.value("weeklyMins", 0u)));
    });
    // The rhythm itself. Read it back through get_settings, which already carries it — a
    // dedicated getter would be a second description of one thing.
    registry.add("set_pomodoro_config", [&state](const json& a) {
        return json(state.set_pomodoro_config(a.at("config").get<PomodoroConfig>()));
    });

    // --- Feedback + config ---
    registry.add("submit_label", [&state](const json& a) {
        auto req = a.at("request").get<LabelRequest>();
        auto sid = detail::validate_required_text("Session ID", req.session_id,
                                                  detail::kMaxSessionIdLen);
        auto notes = detail::validate_optional_text("Label notes", req.notes,
                                                    detail::kMaxLabelNotesLen);
        auto source = std::string(label_source_as_str(label_source_parse(req.source)));
        state.submit_label(sid, req.label, source, notes);
        return json(nullptr);
    });
    registry.add("set_focus_mode", [&state](const json& a) {
        state.set_focus_mode(focus_mode_from_string(a.at("mode").get<std::string>()));
        return json(nullptr);
    });
    registry.add("get_settings", [&state](const json&) { return json(state.settings()); });
    // Roadmap 7.23. Returns the whole settings object rather than null so the UI renders the
    // value the app actually accepted, not the one it optimistically sent.
    registry.add("set_idle_threshold", [&state](const json& a) {
        state.set_idle_threshold_secs(a.at("seconds").get<std::int64_t>());
        return json(state.settings());
    });
    registry.add("get_privacy_settings", [&state](const json&) {
        return json(state.privacy_settings());
    });
    registry.add("get_analytics", [&state](const json& a) {
        return json(state.analytics(a.value("window", std::string("all")),
                                    detail::opt_string(a, "since")));
    });
    registry.add("get_daily_summary", [&state](const json& a) {
        return json(state.daily_summary(a.value("window", std::string("7d")),
                                        detail::opt_string(a, "since")));
    });
    registry.add("get_summary_report", [&state](const json& a) {
        return json(state.summary_report(a.value("window", std::string("day")),
                                         detail::opt_string(a, "since")));
    });
    registry.add_async(
        "export_summary_report",
        [&state, data_dir](const json& a) {
            return json(state.export_summary_report(data_dir / "exports" / "summaries",
                                                     a.value("window", std::string("day")),
                                                     detail::opt_string(a, "since")));
        },
        summary_export_active, "summary export is already in progress");
    registry.add("set_private_mode", [&state](const json& a) {
        state.set_private_mode(a.at("enabled").get<bool>());
        return json(state.privacy_settings());
    });
    registry.add("set_privacy_exclusions", [&state](const json& a) {
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
    registry.add("delete_all_activity_data",
             [&state, training_export_active, training_active, summary_export_active,
              personal_export_active](
                 const json&) {
        if (training_export_active->load(std::memory_order_acquire)) {
            throw std::runtime_error(
                "training export is in progress; wait for it to finish before deleting activity");
        }
        // exports/summaries is on this command's deletion list too.
        if (summary_export_active->load(std::memory_order_acquire)) {
            throw std::runtime_error(
                "summary export is in progress; wait for it to finish before deleting activity");
        }
        // Deleting exports/training under a running pipeline would take its inputs away
        // mid-read and race its outputs. Training only became concurrent with this command
        // when it moved off the UI thread, which is why the check is newer than its sibling.
        if (training_active->load(std::memory_order_acquire)) {
            throw std::runtime_error(
                "training is in progress; wait for it to finish before deleting activity");
        }
        if (personal_export_active->load(std::memory_order_acquire)) {
            throw std::runtime_error(
                "personal data export is in progress; wait for it to finish before deleting activity");
        }
        return json(state.delete_all_activity_data());
    });
    // Roadmap 7.6: "delete everything" was the only eraser available, which makes removing
    // one bad session cost the user their whole history. Reports whether a row was actually
    // removed rather than returning null, so the UI can distinguish a stale list entry from
    // a successful delete instead of silently claiming success for an id that never existed.
    registry.add("delete_session", [&state](const json& a) {
        auto sid = detail::validate_required_text("Session ID",
                                                  a.at("sessionId").get<std::string>(),
                                                  detail::kMaxSessionIdLen);
        return json(state.delete_session(sid));
    });
    // Roadmap 7.6: "local-only" is a claim the user cannot check without being able to reach
    // the files. `path` comes back whether or not the file manager opened, because "we could
    // not open it, here is where it is" is still an answer the user can act on — and on a
    // platform with no backend it is the *only* one.
    registry.add("open_data_folder", [data_dir](const json&) {
        return json{{"path", data_dir.string()},
                    {"supported", reveal_supported()},
                    {"opened", reveal_directory(data_dir)}};
    });
    // Roadmap 8.14. External links leave through the OS browser; this is the native half of
    // the shim's click interceptor. Only http/https/mailto are accepted.
    registry.add("open_external_url", [](const json& a) {
        auto url =
            detail::validate_required_text("URL", a.at("url").get<std::string>(), 4096);
        if (!detail::open_allowed_external_url(url)) {
            throw std::runtime_error("URL is not allowed to open externally");
        }
        return json{{"supported", open_external_url_supported()},
                    {"opened", open_external_url(url)}};
    });
    // Roadmap 10.14. Native OS file pickers for import and export.
    registry.add("pick_open_file", [](const json& a) {
        FileDialogOptions options{};
        if (a.contains("options") && !a.at("options").is_null()) {
            options = a.at("options").get<FileDialogOptions>();
        }
        return json(pick_open_file(options));
    });
    registry.add("pick_save_file", [](const json& a) {
        FileDialogOptions options{};
        if (a.contains("options") && !a.at("options").is_null()) {
            options = a.at("options").get<FileDialogOptions>();
        }
        return json(pick_save_file(options));
    });
    registry.add("get_goal_categories", [&state](const json&) {
        return json(state.goal_categories());
    });
    registry.add("set_goal_categories", [&state](const json& a) {
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
    registry.add("get_autostart", [](const json&) {
        return json{{"enabled", autostart_enabled()}, {"supported", autostart_supported()}};
    });
    registry.add("set_autostart", [](const json& a) {
        const bool enabled = a.at("enabled").get<bool>();
        if (!autostart_supported()) {
            throw std::runtime_error("autostart is not supported on this platform");
        }
        if (!set_autostart_enabled(enabled)) {
            throw std::runtime_error("could not update autostart setting");
        }
        return json{{"enabled", autostart_enabled()}, {"supported", true}};
    });
    registry.add("dismiss_snapback", [&state, dismiss_overlay](const json&) {
        state.dismiss_snapback();
        // Bind callbacks run on the UI thread, so hiding the native overlay is safe here.
        (*dismiss_overlay)();
        return json(nullptr);
    });
    registry.add("restore_snapback_target", [&state, dismiss_overlay](const json&) {
        auto res = state.restore_snapback_target();
        (*dismiss_overlay)();
        return json(res);
    });


    // --- App rules (allow/block overrides) ---
    registry.add("get_app_rules", [&state](const json&) { return json(state.app_rules()); });
    registry.add("upsert_app_rule", [&state](const json& a) {
        auto req = a.at("request").get<UpsertAppRuleRequest>();
        auto pattern = detail::validate_required_text("App rule pattern", req.pattern,
                                                      detail::kMaxAppRulePatternLen);
        auto note = detail::validate_optional_text("App rule note", req.note,
                                                   detail::kMaxAppRuleNoteLen);
        return json(state.upsert_app_rule(pattern, req.rule_type, note));
    });
    registry.add("delete_app_rule", [&state](const json& a) {
        state.delete_app_rule(a.at("id").get<std::int64_t>());
        return json(nullptr);
    });

    // --- Context timeline ---
    registry.add("get_context_timeline", [&state](const json& a) {
        return json(state.context_timeline(detail::opt_string(a, "sessionId"),
                                           detail::clamp_limit(a, 20)));
    });

    // --- ONNX + permissions ---
    registry.add("reload_classifier_model",
             [&state](const json&) { return json(state.reload_classifier_model()); });
    registry.add("refresh_permissions",
             [&state](const json&) { return json(state.refresh_permissions()); });
    // User-initiated only (wizard "Grant access" button) — this one can raise an OS dialog,
    // so it must never be called from a poll the way refresh_permissions is.
    registry.add("request_permissions",
             [&state](const json&) { return json(state.request_permissions()); });

    // --- Training data export ---
    registry.add_async(
        "export_training_data",
        [&state, data_dir](const json& a) {
            const auto out_dir = data_dir / "exports" / "training";
            return json(
                state.export_training_data(out_dir, detail::opt_string(a, "sessionId")));
        },
        training_export_active, "training export is already in progress");

    // Roadmap 9.14. The missing direction. `inspect` is read-only and exists so the
    // confirmation can state what the user is about to adopt *and* what they are about to lose;
    // a destructive replace behind a single unlabelled button is the wrong shape for this.
    registry.add("inspect_data_import", [data_dir](const json& a) {
        const auto candidate = inspect_import_candidate(
            std::filesystem::path(detail::opt_string(a, "path").value_or("")),
            data_dir / "focoflow.db");
        return json{{"acceptable", candidate.acceptable},
                    {"message", candidate.message},
                    {"schemaVersion", candidate.schema_version},
                    {"sessionCount", candidate.session_count}};
    });

    // Staged rather than applied, because the swap cannot happen while this process holds the
    // database open — see data_import.hpp. Returns what will happen at the next launch.
    // Staging is a VACUUM INTO of the whole incoming database: seconds for a mature one, all
    // of it disk-bound, so it runs on the worker. The two commands below that read or remove
    // the staged file check the gate rather than race a copy in progress.
    registry.add_async(
        "stage_data_import",
        [data_dir](const json& a) {
            const auto staged = stage_import(
                std::filesystem::path(detail::opt_string(a, "path").value_or("")),
                data_dir / "focoflow.db", nullptr);
            return json{{"ok", staged.ok},
                        {"message", staged.message},
                        {"schemaVersion", staged.schema_version},
                        {"sessionCount", staged.session_count}};
        },
        import_staging_active, "a data import is already being staged");

    // The undo. A staged import that has not been applied is one file deletion away from never
    // having happened, and the user is entitled to that before they restart.
    registry.add("cancel_data_import", [data_dir, import_staging_active](const json&) {
        if (import_staging_active->load(std::memory_order_acquire)) {
            throw std::runtime_error("a data import is still being staged; wait for it to finish");
        }
        const auto db_path = data_dir / "focoflow.db";
        const bool cancelled = cancel_staged_import(db_path);
        return json{{"cancelled", cancelled}, {"pending", has_staged_import(db_path)}};
    });

    registry.add("get_data_import_status", [data_dir, import_staging_active](const json&) {
        // A stage in progress has already created the file; it is not pending until the copy
        // has finished, and reporting it early would invite a restart onto a half-written one.
        const bool staging = import_staging_active->load(std::memory_order_acquire);
        return json{{"pending", !staging && has_staged_import(data_dir / "focoflow.db")}};
    });

    // Roadmap 7.6: the legible counterpart to export_training_data. Separate command and
    // separate directory because they answer different questions and have different audiences
    // — one is for a training script, this one is for the person being recorded.
    // Reads every session, window, and episode the user has and writes them out. That was a
    // synchronous binding, so a long history on a slow disk held the UI thread for the whole
    // write; it now runs on the same worker the training export uses, behind its own gate.
    registry.add_async(
        "export_my_data",
        [&state, data_dir](const json&) {
            const auto result = state.export_personal_data(data_dir / "exports" / "personal");
            // Roadmap 9.16. Per-record-type omission counts and the body checksum travel with
            // the path. `truncated` is derived from the counts rather than being its own
            // field, so the old failure -- reporting a complete export after dropping windows
            // from an included session -- cannot be expressed on the wire either.
            return json{{"outputPath", result.output_path},
                        {"sessionCount", result.session_count},
                        {"windowCount", result.window_count},
                        {"episodeCount", result.episode_count},
                        {"omittedSessions", result.omitted_sessions},
                        {"omittedWindows", result.omitted_windows},
                        {"checksum", result.checksum},
                        {"truncated", result.truncated()}};
        },
        personal_export_active, "personal data export is already in progress");

    // --- Training pipeline ---
    registry.add("get_training_deploy_status", [data_dir](const json&) {
        if (!developer_tools_enabled()) {
            throw std::runtime_error(
                "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
        }
        return training_deploy::training_deploy_status(data_dir);
    });
    registry.add("set_training_repo_path", [data_dir](const json& a) {
        if (!developer_tools_enabled()) {
            throw std::runtime_error(
                "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
        }
        auto repo_path = detail::validate_required_text(
            "Repo path", a.at("repoPath").get<std::string>(), detail::kMaxRepoPathLen);
        training_deploy::write_training_repo_path(data_dir, repo_path);
        return json(nullptr);
    });
    // Training runs Python for minutes. It was a sync binding, which runs on the webview's
    // own thread: the window froze for the whole run. On the worker it also has to be
    // cancellable, because the runner joins that worker at shutdown -- quitting mid-run
    // would otherwise wait for Python to finish. The predicate reads the runner's state and
    // the user's cancel live, so a job that starts during the shutdown drain, or after a
    // cancel that arrived while it was still queued, ends at its first poll.
    registry.add_async(
        "train_from_export",
        [&state, data_dir, training_export_active, training_cancel_requested,
         &async_commands](const json&) {
            if (training_export_active->load(std::memory_order_acquire)) {
                throw std::runtime_error(
                    "training export is in progress; wait for it to finish before training");
            }
            if (!developer_tools_enabled()) {
                throw std::runtime_error(
                    "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
            }
            // Progress is the log tail, pushed as an event whenever it changes. The result
            // still comes back through this call's promise; the events only fill the wait.
            return training_deploy::train_from_export(
                data_dir,
                [&async_commands, training_cancel_requested] {
                    return async_commands.stopping() ||
                           training_cancel_requested->load(std::memory_order_acquire);
                },
                [&state](const training_deploy::TrainingProgress& progress) {
                    state.emit_event("training-progress",
                                     dump_json(json{{"elapsedMs", progress.elapsed_ms},
                                                    {"logTail", progress.log_tail}}));
                });
        },
        training_active, "training is already in progress",
        [training_cancel_requested] {
            training_cancel_requested->store(false, std::memory_order_release);
        });
    // Reports whether there was a run to cancel. The run itself answers through its own
    // result (`cancelled: true`) once the child is gone; this only raises the request.
    registry.add("cancel_training", [training_active, training_cancel_requested](const json&) {
        if (!developer_tools_enabled()) {
            throw std::runtime_error(
                "training tooling is developer-only; set SNAPBACK_DEV_TRAINING or use a Debug build");
        }
        const bool running = training_active->load(std::memory_order_acquire);
        if (running) training_cancel_requested->store(true, std::memory_order_release);
        return json{{"requested", running}};
    });
    // AUD-16 / P0-08: deliberately NOT developer-gated, unlike the three commands above.
    // ADR-0006 scopes developer tooling to *producing* a model — training, repo-path config,
    // train-from-export, the CLI copy surface. Recovering from a bad deployed model is the
    // other half of that line and belongs to the user: someone whose classifier was ruined by
    // a deployment must not need SNAPBACK_DEV_TRAINING or a Debug build to escape it. The
    // asymmetry with its siblings is the decision, not an oversight.
    registry.add("rollback_classifier_model", [&state, data_dir](const json&) {
        auto result = training_deploy::rollback_model(data_dir);
        result["classifier"] = state.reload_classifier_model();
        return result;
    });
    // Roadmap 13.8. Retries startup-safe deployment cleanup without restarting the app.
    // Ungated for the same reason as rollback above: recovery, not production.
    registry.add("retry_model_deployment_cleanup", [&state](const json&) {
        return json(state.retry_model_deployment_cleanup());
    });
}

}  // namespace snapback
