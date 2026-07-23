// Ports the #[cfg(test)] cases from ../Snapback/src-tauri/src/types.rs, plus the
// wire-format assertions the port plan requires (camelCase keys, nested objects).
#include "doctest_wrapper.hpp"

#include "types.hpp"

using namespace snapback;

// --- LabelSource parse/as_str (types.rs) ------------------------------------

TEST_CASE("label_source_parse recognizes known values case-insensitively") {
    CHECK(label_source_parse("hotkey") == LabelSource::Hotkey);
    CHECK(label_source_parse("HOTKEY") == LabelSource::Hotkey);
    CHECK(label_source_parse("Survey") == LabelSource::Survey);
    CHECK(label_source_parse("AUTO") == LabelSource::Auto);
    CHECK(label_source_parse("manual") == LabelSource::Manual);
}

TEST_CASE("label_source_parse falls back to Manual for none/empty/unknown") {
    CHECK(label_source_parse(std::nullopt) == LabelSource::Manual);
    CHECK(label_source_parse("") == LabelSource::Manual);
    CHECK(label_source_parse("bogus") == LabelSource::Manual);
}

TEST_CASE("label_source as_str round-trips through parse") {
    for (auto s : {LabelSource::Manual, LabelSource::Hotkey, LabelSource::Survey,
                   LabelSource::Auto}) {
        CHECK(label_source_parse(std::string(label_source_as_str(s))) == s);
    }
}

// --- FocusMode (types.rs) ---------------------------------------------------

TEST_CASE("focus_mode_from_string recognizes known values case-insensitively") {
    CHECK(focus_mode_from_string("deep") == FocusMode::Deep);
    CHECK(focus_mode_from_string("DEEP") == FocusMode::Deep);
    CHECK(focus_mode_from_string("recovery") == FocusMode::Recovery);
    CHECK(focus_mode_from_string("normal") == FocusMode::Normal);
}

TEST_CASE("focus_mode_from_string falls back to Normal for unknown") {
    CHECK(focus_mode_from_string("") == FocusMode::Normal);
    CHECK(focus_mode_from_string("bogus") == FocusMode::Normal);
}

TEST_CASE("focus_mode string form matches JSON serialization") {
    for (auto m : {FocusMode::Deep, FocusMode::Normal, FocusMode::Recovery}) {
        json j = m;  // enum -> JSON string
        CHECK(std::string(focus_mode_to_string(m)) == j.get<std::string>());
    }
}

TEST_CASE("focus_mode thresholds and hyperfocus minutes are stable per variant") {
    CHECK(risk_threshold(FocusMode::Deep) == doctest::Approx(0.55));
    CHECK(risk_threshold(FocusMode::Normal) == doctest::Approx(0.70));
    CHECK(risk_threshold(FocusMode::Recovery) == doctest::Approx(0.85));
    CHECK(hyperfocus_minutes(FocusMode::Deep) == 90);
    CHECK(hyperfocus_minutes(FocusMode::Normal) == 120);
    CHECK(hyperfocus_minutes(FocusMode::Recovery) == 45);
}

// --- AppRuleKind (types.rs) -------------------------------------------------

TEST_CASE("app_rule_kind_from_string recognizes known values case-insensitively") {
    CHECK(app_rule_kind_from_string("allow") == AppRuleKind::Allow);
    CHECK(app_rule_kind_from_string("ALLOW") == AppRuleKind::Allow);
    CHECK(app_rule_kind_from_string("block") == AppRuleKind::Block);
}

TEST_CASE("app_rule_kind_from_string returns nullopt for unknown (not a default)") {
    CHECK(app_rule_kind_from_string("") == std::nullopt);
    CHECK(app_rule_kind_from_string("bogus") == std::nullopt);
}

TEST_CASE("app_rule_kind string form matches JSON serialization") {
    for (auto k : {AppRuleKind::Allow, AppRuleKind::Block}) {
        json j = k;
        CHECK(std::string(app_rule_kind_to_string(k)) == j.get<std::string>());
    }
}

// --- Enum wire strings match Rust serde exactly -----------------------------

TEST_CASE("EventType and FocusLabel serialize to the exact Rust strings") {
    CHECK(json(EventType::KeyPress).get<std::string>() == "KEY_PRESS");
    CHECK(json(EventType::WindowFocusChange).get<std::string>() == "WINDOW_FOCUS_CHANGE");
    CHECK(json(EventType::IdleEnd).get<std::string>() == "IDLE_END");
    CHECK(json(FocusLabel::Distracted).get<std::string>() == "DISTRACTED");
    CHECK(json(FocusLabel::DeepFocus).get<std::string>() == "DEEP_FOCUS");
    // Distracted is -1 in Rust; the numeric value backs the enum, string backs JSON.
    CHECK(static_cast<int>(FocusLabel::Distracted) == -1);
}

// --- Struct wire format: camelCase keys -------------------------------------

TEST_CASE("PredictionRecord serializes with camelCase keys and round-trips") {
    PredictionRecord p;
    p.session_id = "s1";
    p.focus_score = 72.0;
    p.distraction_risk = 0.3;
    p.focus_state = "DEEP_FOCUS";
    p.thrash_score = 0.1;
    p.drift_score = 0.2;
    p.goal_alignment = 0.8;
    p.timestamp = "2026-07-11T00:00:00Z";

    json j = p;
    CHECK(j.contains("sessionId"));
    CHECK(j.contains("focusScore"));
    CHECK(j.contains("distractionRisk"));
    CHECK(j.contains("focusState"));
    CHECK(j.contains("goalAlignment"));
    CHECK_FALSE(j.contains("session_id"));  // must NOT be snake_case

    auto back = j.get<PredictionRecord>();
    CHECK(back.session_id == "s1");
    CHECK(back.focus_score == doctest::Approx(72.0));
    CHECK(back.focus_state == "DEEP_FOCUS");
    CHECK(back.goal_alignment == doctest::Approx(0.8));
}

TEST_CASE("PredictionRecord defaults goalAlignment to 0.5 when absent") {
    json j = json::parse(R"({"sessionId":"s","focusScore":1,"distractionRisk":0,
                             "focusState":"PRODUCTIVE","timestamp":"t"})");
    auto p = j.get<PredictionRecord>();
    CHECK(p.goal_alignment == doctest::Approx(0.5));
}

TEST_CASE("HealthStatus nests permissions and classifier as camelCase objects") {
    HealthStatus h;
    h.status = "ok";
    h.capture_running = true;
    h.capture_events_dropped = 5;
    h.last_prediction_age_secs = 1.5;
    h.prediction_suppression_reason = "none";
    h.permissions.capture_available = true;
    h.permissions.setup_steps = {"grant access"};
    h.classifier.backend = "heuristic";
    h.classifier.onnx_runtime_enabled = false;

    json j = h;
    CHECK(j.contains("captureRunning"));
    CHECK(j.contains("captureEventsDropped"));
    CHECK(j["lastPredictionAgeSecs"] == doctest::Approx(1.5));
    CHECK(j["predictionSuppressionReason"] == "none");
    CHECK(j["permissions"].contains("captureAvailable"));
    CHECK(j["permissions"].contains("setupSteps"));
    CHECK(j["classifier"].contains("onnxRuntimeEnabled"));
    // Optional-null fields present as JSON null (matches serde Option<String>).
    CHECK(j["captureFailureReason"].is_null());

    auto back = j.get<HealthStatus>();
    CHECK(back.capture_running);
    CHECK(back.capture_events_dropped == 5);
    REQUIRE(back.last_prediction_age_secs.has_value());
    CHECK(*back.last_prediction_age_secs == doctest::Approx(1.5));
    CHECK(back.prediction_suppression_reason == "none");
    CHECK(back.permissions.capture_available);
    CHECK(back.permissions.setup_steps.size() == 1);
    CHECK(back.classifier.backend == "heuristic");
}

TEST_CASE("SessionRecord optional timestamps round-trip as null when absent") {
    SessionRecord s;
    s.session_id = "s1";
    s.goal = "ship it";
    s.status = "ACTIVE";
    s.focus_mode = "deep";
    // started_at / ended_at left nullopt

    json j = s;
    CHECK(j.contains("focusMode"));
    CHECK(j["startedAt"].is_null());
    CHECK(j["endedAt"].is_null());

    auto back = j.get<SessionRecord>();
    CHECK(back.started_at == std::nullopt);
    CHECK(back.focus_mode == "deep");
}

TEST_CASE("AppSettings serializes default focus mode as camelCase") {
    AppSettings settings;
    settings.default_focus_mode = FocusMode::Recovery;

    json j = settings;
    CHECK(j.contains("defaultFocusMode"));
    CHECK_FALSE(j.contains("default_focus_mode"));
    CHECK(j["defaultFocusMode"].get<std::string>() == "recovery");

    auto back = j.get<AppSettings>();
    CHECK(back.default_focus_mode == FocusMode::Recovery);

    auto missing = json::object().get<AppSettings>();
    CHECK(missing.default_focus_mode == FocusMode::Normal);
}

TEST_CASE("LabelRequest carries the nested camelCase arg shape") {
    json j = json::parse(R"({"sessionId":"s1","label":"DEEP_FOCUS","source":"hotkey"})");
    auto req = j.get<LabelRequest>();
    CHECK(req.session_id == "s1");
    CHECK(req.label == FocusLabel::DeepFocus);
    CHECK(req.source == std::optional<std::string>("hotkey"));
    CHECK(req.notes == std::nullopt);
}
