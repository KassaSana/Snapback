#include "doctest_wrapper.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "storage/storage.hpp"
#include "util/logger.hpp"

using namespace snapback;

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("snapback_cpp_storage_test_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

PredictionRecord prediction(const std::string& session_id, double focus, double risk,
                            const std::string& state) {
    PredictionRecord p;
    p.session_id = session_id;
    p.focus_score = focus;
    p.distraction_risk = risk;
    p.focus_state = state;
    p.thrash_score = risk >= 0.7 ? 1.0 : 0.0;
    p.drift_score = 0.1;
    p.goal_alignment = 0.6;
    p.timestamp = "2026-07-11T19:00:00Z";
    return p;
}

}  // namespace

TEST_CASE("Storage::open creates focoflow.db and migrates the schema") {
    TempDir temp;

    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());

    CHECK(std::filesystem::exists(temp.path / "focoflow.db"));
    CHECK(storage->active_session() == std::nullopt);
}

TEST_CASE("Storage::open runs the DB in WAL mode") {
    TempDir temp;
    auto storage = Storage::open(temp.path);
    REQUIRE(storage.has_value());

    // A write in WAL mode creates the "-wal" sidecar next to the DB; its presence is a
    // dependency-free proof that journal_mode=WAL took effect (vs the default rollback
    // journal). WAL + synchronous=NORMAL is what drops per-tick write latency ~50x.
    storage->create_session("wal check", FocusMode::Normal);
    CHECK(std::filesystem::exists(temp.path / "focoflow.db-wal"));
}

TEST_CASE("storage session lifecycle keeps only one active session") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto first = storage->create_session("First goal", FocusMode::Normal);
    CHECK(first.status == "ACTIVE");
    CHECK(first.focus_mode == "normal");
    CHECK(first.started_at.has_value());
    CHECK(first.ended_at == std::nullopt);

    auto second = storage->create_session("Second goal", FocusMode::Deep);
    auto active = storage->active_session();
    REQUIRE(active.has_value());
    CHECK(active->session_id == second.session_id);
    CHECK(active->goal == "Second goal");
    CHECK(active->focus_mode == "deep");

    storage->end_session(second.session_id);
    CHECK(storage->active_session() == std::nullopt);
}

TEST_CASE("storage gates prediction and feature writes to active sessions") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    CHECK_THROWS_AS(storage->insert_prediction(prediction("missing", 50.0, 0.2, "PRODUCTIVE")),
                    std::runtime_error);

    auto session = storage->create_session("Ship storage", FocusMode::Normal);
    CHECK_NOTHROW(storage->insert_prediction(
        prediction(session.session_id, 75.0, 0.2, "PRODUCTIVE")));

    FeatureVector f;
    f.seconds_since_session_start() = 120.0;
    f.keystroke_rate() = 2.5;
    f.is_ide() = 1.0;
    f.focus_momentum() = 0.75;
    CHECK_NOTHROW(storage->insert_feature_snapshot(session.session_id, f));

    storage->end_session(session.session_id);
    CHECK_THROWS_AS(storage->insert_prediction(
                        prediction(session.session_id, 60.0, 0.3, "PRODUCTIVE")),
                    std::runtime_error);
    CHECK_THROWS_AS(storage->insert_feature_snapshot(session.session_id, f),
                    std::runtime_error);
}

TEST_CASE("storage recap computes averages, deep-focus percentage, and thrash spikes") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session = storage->create_session("Measure focus", FocusMode::Recovery);
    storage->insert_prediction(prediction(session.session_id, 90.0, 0.10, "DEEP_FOCUS"));
    storage->insert_prediction(prediction(session.session_id, 70.0, 0.20, "PRODUCTIVE"));
    storage->insert_prediction(prediction(session.session_id, 30.0, 0.80, "DISTRACTED"));

    const auto recap = storage->recap(session.session_id);
    CHECK(recap.session_id == session.session_id);
    CHECK(recap.goal == "Measure focus");
    CHECK(recap.avg_focus_score == doctest::Approx((90.0 + 70.0 + 30.0) / 3.0));
    CHECK(recap.avg_distraction_risk == doctest::Approx((0.10 + 0.20 + 0.80) / 3.0));
    CHECK(recap.deep_focus_pct == doctest::Approx(100.0 / 3.0));
    CHECK(recap.thrash_spikes == 1);
}

TEST_CASE("Storage::infer_session_label maps recap thresholds like Rust") {
    SessionRecap deep;
    deep.session_id = "s";
    deep.goal = "focus";
    deep.duration_secs = 3600;
    deep.avg_focus_score = 80.0;
    deep.avg_distraction_risk = 0.2;
    deep.snapback_count = 0;
    deep.thrash_spikes = 0;
    deep.deep_focus_pct = 60.0;
    CHECK(Storage::infer_session_label(deep) == FocusLabel::DeepFocus);

    SessionRecap distracted = deep;
    distracted.avg_distraction_risk = 0.75;
    distracted.thrash_spikes = 4;
    distracted.deep_focus_pct = 10.0;
    CHECK(Storage::infer_session_label(distracted) == FocusLabel::Distracted);

    SessionRecap pseudo = deep;
    pseudo.avg_distraction_risk = 0.4;
    pseudo.thrash_spikes = 1;
    pseudo.deep_focus_pct = 10.0;
    CHECK(Storage::infer_session_label(pseudo) == FocusLabel::PseudoProductive);

    SessionRecap productive = deep;
    productive.avg_distraction_risk = 0.4;
    productive.thrash_spikes = 0;
    productive.deep_focus_pct = 30.0;
    CHECK(Storage::infer_session_label(productive) == FocusLabel::Productive);
}

TEST_CASE("Storage::save_auto_session_label writes an AUTO label from recap") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session = storage->create_session("Auto label", FocusMode::Normal);
    storage->insert_prediction(prediction(session.session_id, 90.0, 0.10, "DEEP_FOCUS"));
    storage->insert_prediction(prediction(session.session_id, 85.0, 0.15, "DEEP_FOCUS"));
    storage->end_session(session.session_id);

    const FocusLabel label = storage->save_auto_session_label(session.session_id);
    CHECK(label == FocusLabel::DeepFocus);

    TempDir temp;
    const auto exported =
        storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.label_count == 1);

    const auto labels = read_file(temp.path / "labels.csv");
    CHECK(labels.find(",auto,") != std::string::npos);
    CHECK(labels.find("inferred from session recap") != std::string::npos);
}

TEST_CASE("storage exports feature snapshots and labels as CSV") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session = storage->create_session("Export training", FocusMode::Deep);

    FeatureVector f;
    f.seconds_since_session_start() = 240.0;
    f.keystroke_rate() = 3.25;
    f.is_ide() = 1.0;
    f.focus_momentum() = 0.9;
    storage->insert_feature_snapshot(session.session_id, f);
    storage->insert_label(session.session_id, FocusLabel::DeepFocus, "manual");

    TempDir temp;
    storage->export_training_csv(temp.path, std::nullopt);

    const auto features = read_file(temp.path / "features.csv");
    CHECK(features.find("timestamp,seconds_since_session_start") != std::string::npos);
    CHECK(features.find("session_id,session_goal,focus_mode") != std::string::npos);
    CHECK(features.find("Export training") != std::string::npos);
    CHECK(features.find("deep") != std::string::npos);

    const auto labels = read_file(temp.path / "labels.csv");
    CHECK(labels.find("timestamp,label,source,session_id,notes") != std::string::npos);
    CHECK(labels.find(",2,manual,") != std::string::npos);
}

TEST_CASE("storage export_training_csv filters by session id") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto session_a = storage->create_session("Session A", FocusMode::Normal);
    FeatureVector fa;
    fa.seconds_since_session_start() = 60.0;
    fa.keystroke_rate() = 2.0;
    storage->insert_feature_snapshot(session_a.session_id, fa);
    storage->insert_label(session_a.session_id, FocusLabel::Productive, "manual");
    storage->end_session(session_a.session_id);

    auto session_b = storage->create_session("Session B", FocusMode::Deep);
    FeatureVector fb;
    fb.seconds_since_session_start() = 120.0;
    fb.keystroke_rate() = 3.0;
    storage->insert_feature_snapshot(session_b.session_id, fb);
    storage->end_session(session_b.session_id);

    TempDir temp_a;
    const auto export_a =
        storage->export_training_csv(temp_a.path, session_a.session_id);
    CHECK(export_a.feature_count == 1);
    CHECK(export_a.label_count == 1);
    const auto labels_a = read_file(temp_a.path / "labels.csv");
    CHECK(labels_a.find(session_a.session_id) != std::string::npos);
    CHECK(labels_a.find(session_b.session_id) == std::string::npos);

    TempDir temp_all;
    const auto export_all = storage->export_training_csv(temp_all.path, std::nullopt);
    CHECK(export_all.feature_count == 2);
    CHECK(export_all.label_count == 1);
    const auto features_all = read_file(temp_all.path / "features.csv");
    CHECK(features_all.find("Session A") != std::string::npos);
    CHECK(features_all.find("Session B") != std::string::npos);
}

TEST_CASE("storage prune_runtime_data removes old rows from all three runtime tables") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto session = storage->create_session("Retention", FocusMode::Normal);

    PredictionRecord old_pred = prediction(session.session_id, 50.0, 0.2, "PRODUCTIVE");
    old_pred.timestamp = "2020-01-01T00:00:00Z";
    storage->insert_prediction(old_pred);

    ContextSnapshotDto old_ctx;
    old_ctx.app_name = "Cursor";
    old_ctx.window_title = "old.cpp";
    old_ctx.summary = "old context";
    old_ctx.timestamp = "2020-01-01T00:00:00Z";
    storage->save_context_snapshot(session.session_id, old_ctx);

    // insert_feature_snapshot stamps rows with unix_now_secs(), so this row is "now" and
    // must survive a cutoff in the past.
    FeatureVector f;
    f.seconds_since_session_start() = 10.0;
    storage->insert_feature_snapshot(session.session_id, f);

    // 2024-01-01 as RFC3339 and as Unix epoch seconds — the same instant in the two
    // formats the tables use.
    constexpr double kCutoffUnix = 1704067200.0;
    const PruneSummary summary =
        storage->prune_runtime_data("2024-01-01T00:00:00Z", kCutoffUnix);
    CHECK(summary.predictions_deleted == 1);
    CHECK(summary.context_snapshots_deleted == 1);
    CHECK(summary.feature_snapshots_deleted == 0);  // stamped now, newer than the cutoff
    CHECK(should_vacuum_after_prune(summary.total()) == false);

    TempDir temp;
    const auto exported =
        storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.feature_count == 1);
}

TEST_CASE("storage prune_runtime_data deletes feature snapshots past the cutoff") {
    // The regression this closes: feature_snapshots is the highest-volume table (one row
    // per prediction tick) and was excluded from the prune entirely, so it grew forever
    // while predictions/context_snapshots stayed flat at the retention window.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto session = storage->create_session("Retention", FocusMode::Normal);
    FeatureVector f;
    f.seconds_since_session_start() = 10.0;
    storage->insert_feature_snapshot(session.session_id, f);

    // A cutoff far in the future makes the just-written row "old".
    constexpr double kFarFuture = 4102444800.0;  // 2100-01-01
    const PruneSummary summary =
        storage->prune_runtime_data("2100-01-01T00:00:00Z", kFarFuture);
    CHECK(summary.feature_snapshots_deleted == 1);

    TempDir temp;
    const auto exported =
        storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.feature_count == 0);
}

TEST_CASE("should_vacuum_after_prune matches Rust threshold") {
    CHECK_FALSE(should_vacuum_after_prune(0));
    CHECK_FALSE(should_vacuum_after_prune(kVacuumMinDeletedRows - 1));
    CHECK(should_vacuum_after_prune(kVacuumMinDeletedRows));
}

TEST_CASE("Storage::open routes the startup prune message through an injected logger") {
    TempDir temp;
    {
        // Seed one prediction old enough for the on-open prune (kDefaultRetentionDays)
        // to catch, then let this Storage go out of scope so the reopen below is a real
        // cold start against the file on disk.
        auto storage = Storage::open(temp.path);
        REQUIRE(storage.has_value());
        const auto session = storage->create_session("Retention", FocusMode::Normal);
        PredictionRecord old_pred = prediction(session.session_id, 50.0, 0.2, "PRODUCTIVE");
        old_pred.timestamp = "2000-01-01T00:00:00Z";
        storage->insert_prediction(old_pred);
    }

    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info, [] { return std::string("2026-07-19T00:00:00Z"); });
    auto reopened = Storage::open(temp.path, &logger);
    REQUIRE(reopened.has_value());

    CHECK(log_out.str().find("[INFO]") != std::string::npos);
    CHECK(log_out.str().find("pruned 1 rows") != std::string::npos);
}

TEST_CASE("storage schema indexes the hot read paths") {
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    const auto names = storage->index_names();
    auto has = [&](const std::string& name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    CHECK(has("idx_predictions_session_ts"));
    CHECK(has("idx_predictions_ts"));
    CHECK(has("idx_feature_snapshots_session_ts"));
    CHECK(has("idx_sessions_status_started"));
    CHECK(has("idx_context_snapshots_session_ts"));
    CHECK(has("idx_snapback_events_session"));
    CHECK(has("idx_labels_session"));
}

TEST_CASE("storage hot queries use an index instead of scanning") {
    // Presence isn't enough — a composite index whose leading column the query doesn't
    // filter on is unusable, which is exactly why latest_prediction() scanned despite
    // idx_predictions_session_ts existing. Assert the planner actually picks one, and
    // that no query needs a temp B-tree to satisfy its ORDER BY.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());

    auto uses_index = [&](const std::string& sql) {
        bool indexed = false;
        for (const auto& step : storage->query_plan(sql)) {
            if (step.find("USING INDEX") != std::string::npos ||
                step.find("USING COVERING INDEX") != std::string::npos) {
                indexed = true;
            }
            // A temp B-tree means the index didn't supply the ordering.
            CHECK(step.find("TEMP B-TREE") == std::string::npos);
        }
        return indexed;
    };

    CHECK(uses_index("SELECT session_id FROM predictions ORDER BY timestamp DESC LIMIT 1"));
    CHECK(uses_index(
        "SELECT session_id FROM sessions WHERE status = 'ACTIVE' ORDER BY started_at DESC LIMIT 1"));
    CHECK(uses_index(
        "SELECT app_name FROM context_snapshots WHERE session_id = 'x' ORDER BY timestamp ASC"));
    CHECK(uses_index(
        "SELECT COUNT(*) FROM snapback_events WHERE session_id = 'x'"));
    CHECK(uses_index("SELECT id FROM labels WHERE session_id = 'x'"));
}

TEST_CASE("Storage::open explains why it failed instead of returning a bare nullopt") {
    // The old outer `catch (...)` returned nullopt with no diagnostic, so a corrupt DB, a
    // permissions problem, and a full disk all looked identical — the user just saw the app
    // decline to start. Force a real failure by putting a directory where the DB file goes:
    // sqlite can't open it, and the reason must reach the injected logger.
    TempDir temp;
    std::filesystem::create_directories(temp.path / "focoflow.db");

    std::ostringstream log_out;
    Logger logger(log_out, LogLevel::Info);
    auto storage = Storage::open(temp.path, &logger);

    CHECK_FALSE(storage.has_value());
    const auto logged = log_out.str();
    CHECK(logged.find("ERROR") != std::string::npos);
    CHECK(logged.find("focoflow.db") != std::string::npos);
}

TEST_CASE("export reports a write failure instead of a bogus success") {
    // Only the open() was checked before, so a mid-write failure (classically a full disk)
    // left a truncated file behind while export still returned a success result whose
    // feature_count described rows that never made it to disk.
    //
    // Simulate an unwritable destination by making labels.csv a directory: the open
    // succeeds or fails depending on platform, but either way export must not claim success.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("Export failure", FocusMode::Normal);
    FeatureVector f;
    f.seconds_since_session_start() = 5.0;
    storage->insert_feature_snapshot(session.session_id, f);

    TempDir temp;
    std::filesystem::create_directories(temp.path / "labels.csv");
    CHECK_THROWS_AS(storage->export_training_csv(temp.path, session.session_id),
                    std::runtime_error);
}

TEST_CASE("export still succeeds on a writable destination") {
    // Guards the check above from being over-eager: a normal export must stay clean.
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    const auto session = storage->create_session("Export ok", FocusMode::Normal);
    FeatureVector f;
    f.seconds_since_session_start() = 5.0;
    storage->insert_feature_snapshot(session.session_id, f);

    TempDir temp;
    const auto exported = storage->export_training_csv(temp.path, session.session_id);
    CHECK(exported.feature_count == 1);
    CHECK(std::filesystem::is_regular_file(temp.path / "features.csv"));
    CHECK(std::filesystem::is_regular_file(temp.path / "labels.csv"));
}
