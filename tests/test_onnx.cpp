// Tests the ONNX backend. resolve_model_path is exercised in every build; the actual
// load+run + heuristic-fallback path only when SNAPBACK_ONNX is on (fixtures/model.onnx).
#include "doctest_wrapper.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#include "app/state.hpp"
#include "app_state_test_access.hpp"
#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "engine/onnx_model.hpp"
#include "storage/storage.hpp"

using namespace snapback;

namespace {
std::filesystem::path make_temp_dir() {
    auto dir = std::filesystem::temp_directory_path() /
               ("snapback_onnx_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir;
}
void touch(const std::filesystem::path& p) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}
}  // namespace

TEST_CASE("resolve_model_path ignores ungated training-export candidates") {
    auto dir = make_temp_dir();
    CHECK(OnnxModel::resolve_model_path(dir) == std::nullopt);

    touch(dir / "exports" / "training" / "model.onnx");
    CHECK(OnnxModel::resolve_model_path(dir) == std::nullopt);

    touch(dir / "model.onnx");
    CHECK(OnnxModel::resolve_model_path(dir) == dir / "model.onnx");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("model identity is stable for content and includes the feature contract") {
    auto dir = make_temp_dir();
    const auto path = dir / "model.onnx";
    touch(path);

    const auto first = OnnxModel::model_id_for_path(path);
    REQUIRE(first.has_value());
    CHECK(first->find("onnx:snapback-features-v1-31:") == 0);
    CHECK(first == OnnxModel::model_id_for_path(path));

    touch(dir / "other.onnx");
    CHECK(first == OnnxModel::model_id_for_path(dir / "other.onnx"));
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "different";
    CHECK(first != OnnxModel::model_id_for_path(path));
    CHECK_FALSE(OnnxModel::model_id_for_path(dir / "missing.onnx").has_value());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("model outputs are accepted only when they are usable class weights") {
    // Four floats came back from the graph and were passed straight to the classifier,
    // which normalises them by their sum and takes the argmax. NaN compares false with
    // everything, a negative entry corrupts the normalisation, and an all-zero row ranks
    // nothing -- each of those used to become a confident-looking prediction. Entries above
    // one are fine: the fixture model is an unnormalised linear head and its output for a
    // busy window is {0.1, 0.15, 5.55, 0.2}. The check is a property of the model's output
    // contract, so it lives with the model rather than in scoring.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(OnnxModel::valid_class_probabilities({0.1, 0.2, 0.3, 0.4}));
    CHECK(OnnxModel::valid_class_probabilities({0.0, 0.0, 1.0, 0.0}));
    CHECK(OnnxModel::valid_class_probabilities({0.25, 0.25, 0.25, 0.25}));
    CHECK(OnnxModel::valid_class_probabilities({0.1, 0.15, 5.55, 0.2}));

    CHECK_FALSE(OnnxModel::valid_class_probabilities({nan, 0.2, 0.3, 0.4}));
    CHECK_FALSE(OnnxModel::valid_class_probabilities({0.1, inf, 0.3, 0.4}));
    CHECK_FALSE(OnnxModel::valid_class_probabilities({-0.1, 0.4, 0.4, 0.3}));
    CHECK_FALSE(OnnxModel::valid_class_probabilities({0.0, 0.0, 0.0, 0.0}));
}

TEST_CASE("rejected or failed inference is counted and flips the degraded flag") {
    struct Guard {
        ~Guard() { OnnxModel::instance().reset_for_tests(); }
    } guard;
    auto& model = OnnxModel::instance();
    model.reset_for_tests();
    CHECK_FALSE(model.last_inference_failed());
    CHECK(model.inference_failures() == 0);

    // A throwing Run() arrives here as nullopt; a rejected row arrives as a value.
    CHECK_FALSE(model.accept_output(std::nullopt).has_value());
    CHECK(model.last_inference_failed());
    CHECK(model.inference_failures() == 1);

    CHECK_FALSE(model.accept_output(std::array<double, 4>{0.0, 0.0, 0.0, 0.0}).has_value());
    CHECK(model.inference_failures() == 2);

    // One good row clears the flag but keeps the history.
    const auto ok = model.accept_output(std::array<double, 4>{0.1, 0.2, 0.3, 0.4});
    REQUIRE(ok.has_value());
    CHECK((*ok)[3] == doctest::Approx(0.4));
    CHECK_FALSE(model.last_inference_failed());
    CHECK(model.inference_failures() == 2);

    // Unloading starts a new model's history from zero.
    model.unload();
    CHECK_FALSE(model.last_inference_failed());
    CHECK(model.inference_failures() == 0);
}

TEST_CASE("the classifier reports the backend that made the last prediction") {
    // Without a loaded model there is nothing to degrade, in either build.
    struct Guard {
        ~Guard() { OnnxModel::instance().reset_for_tests(); }
    } guard;
    OnnxModel::instance().reset_for_tests();
    Classifier clf;
    CHECK(clf.backend() == "heuristic");
    CHECK_FALSE(clf.inference_degraded());
    CHECK(clf.inference_failures() == 0);
}

#if defined(SNAPBACK_ONNX)
TEST_CASE("ONNX backend loads the fixture, runs it, and falls back to heuristic when reset") {
    // The singleton persists across tests, so always reset on the way out.
    struct Guard {
        ~Guard() { OnnxModel::instance().reset_for_tests(); }
    } guard;

    Classifier clf;
    OnnxModel::instance().reset_for_tests();
    CHECK(clf.backend() == "heuristic");  // no model loaded -> fallback

    const auto path = std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "model.onnx";
    REQUIRE(std::filesystem::is_regular_file(path));
    REQUIRE(OnnxModel::instance().init(path));
    CHECK(OnnxModel::instance().loaded());
    CHECK(clf.backend() == "onnx");  // model loaded -> ONNX backend

    FeatureVector features;
    features.keystroke_count() = 10.0;
    const auto scores = clf.predict(features, FocusMode::Normal);

    CHECK(scores.focus_score >= 0.0);
    CHECK(scores.focus_score <= 100.0);
    const bool valid_state =
        scores.focus_state == "DISTRACTED" || scores.focus_state == "PSEUDO_PRODUCTIVE" ||
        scores.focus_state == "PRODUCTIVE" || scores.focus_state == "DEEP_FOCUS";
    CHECK(valid_state);

    // The fixture ran and its output passed validation: the model is healthy and the
    // health snapshot may say "onnx".
    CHECK_FALSE(OnnxModel::instance().last_inference_failed());
    CHECK(clf.backend() == "onnx");
    CHECK_FALSE(clf.inference_degraded());

    // A rejected output while the model stays loaded: the backend reported is the one that
    // made the prediction (the heuristic), and the status says why.
    CHECK_FALSE(OnnxModel::instance()
                    .accept_output(std::array<double, 4>{0.0, 0.0, 0.0, 0.0})
                    .has_value());
    CHECK(OnnxModel::instance().loaded());
    CHECK(clf.backend() == "heuristic");
    CHECK(clf.inference_degraded());
    CHECK(clf.inference_failures() == 1);
    CHECK(clf.model_id() == "heuristic:" + std::string(kFeatureContractId));

    // The next successful inference restores it.
    CHECK(clf.predict(features, FocusMode::Normal).focus_score >= 0.0);
    CHECK(clf.backend() == "onnx");
    CHECK_FALSE(clf.inference_degraded());
    CHECK(clf.model_id().find("onnx:") == 0);
}
#endif

#if defined(SNAPBACK_ONNX)
TEST_CASE("effective ONNX provenance reaches the persisted prediction row") {
    const auto temp = make_temp_dir();

    struct TempDirGuard {
        ~TempDirGuard() { OnnxModel::instance().reset_for_tests(); }
    } guard;

    const auto model = temp / "model.onnx";
    std::filesystem::copy_file(std::filesystem::path(SNAPBACK_FIXTURES_DIR) / "model.onnx",
                               model);
    auto storage = Storage::open_memory();
    REQUIRE(storage.has_value());
    AppState state(std::move(*storage), temp);
    REQUIRE(state.reload_classifier_model().backend == "onnx");

    state.start_session("persist model provenance", FocusMode::Normal);
    CaptureEvent event;
    event.event_type = EventType::KeyPress;
    event.timestamp_secs = 1.0;
    event.app_name = "Cursor";
    event.window_title = "classifier.cpp";
    AppStateTestAccess::process_event(state, event);

    const auto latest = state.latest_prediction();
    REQUIRE(latest.has_value());
    const auto stored = state.prediction_history(1);
    REQUIRE(stored.size() == 1);
    CHECK(latest->model_id.find("onnx:") == 0);
    CHECK(stored.front().model_id == latest->model_id);
}
#endif

TEST_CASE("OnnxModel::run reports failure as nullopt, not as neutral scores") {
    // The distinction the type now carries: "inference failed" vs "inference said
    // neutral". With no model loaded there is nothing to run, and the caller must be able
    // to tell — previously this returned default-constructed scores whose focus_state was
    // the empty string, which then reached the DB.
    struct Guard {
        ~Guard() { OnnxModel::instance().reset_for_tests(); }
    } guard;
    OnnxModel::instance().reset_for_tests();

    FeatureVector features;
    features.keystroke_count() = 5.0;
    CHECK_FALSE(OnnxModel::instance().infer_probabilities(features).has_value());
}

TEST_CASE("model output respects a user Block rule") {
    // The bug this closes lived inside `#if defined(SNAPBACK_ONNX)`, so it was invisible to
    // every default build: the ONNX branch called the guardrails with thrash=0, drift=0,
    // personal_block=false and dropped the goal and rules entirely. Deploying a trained
    // model therefore silently switched OFF the user's Block app rules.
    //
    // blend_model_output is a free function precisely so this is testable with no model
    // loaded and no ONNX compiled in.
    FeatureVector features;
    features.app_name = "YouTube";
    features.window_title = "some video";
    features.keystroke_count() = 5.0;

    // A confident DEEP_FOCUS prediction from the model.
    const std::array<double, 4> confident_deep{0.02, 0.03, 0.05, 0.90};

    const std::vector<AppRuleRecord> none;
    const auto unblocked = compute_context_signals(features, std::nullopt, none, {});
    CHECK_FALSE(unblocked.personal_block);
    CHECK(blend_model_output(confident_deep, unblocked, FocusMode::Normal).focus_state ==
          "DEEP_FOCUS");

    AppRuleRecord block;
    block.id = 1;
    block.pattern = "youtube";
    block.rule_type = AppRuleKind::Block;
    const std::vector<AppRuleRecord> rules{block};

    const auto blocked = compute_context_signals(features, std::nullopt, rules, {});
    CHECK(blocked.personal_block);
    // Same model output, but the user said "this app is a distraction" — and that wins.
    CHECK(blend_model_output(confident_deep, blocked, FocusMode::Normal).focus_state ==
          "DISTRACTED");
}

TEST_CASE("model output carries real goal alignment, not a pinned 0.5") {
    // goal_alignment used to be hardcoded to 0.5 on the ONNX path, so the UI showed a
    // constant neutral alignment no matter what the user was doing.
    FeatureVector on_goal;
    on_goal.app_name = "Cursor";
    on_goal.window_title = "classifier.cpp — snapback";

    // "implement" is a keyword of the built-in "coding" category, and Cursor classifies as
    // an IDE — so this pair must score above neutral.
    const auto signals = compute_context_signals(
        on_goal, std::optional<std::string>("implement the classifier"), {}, {});
    const auto scores =
        blend_model_output({0.1, 0.1, 0.4, 0.4}, signals, FocusMode::Normal);

    CHECK(scores.goal_alignment == doctest::Approx(signals.goal_alignment));
    CHECK(scores.goal_alignment != doctest::Approx(0.5));
    // thrash/drift must survive onto the record too — they're persisted per prediction.
    CHECK(scores.thrash_score == doctest::Approx(signals.thrash));
    CHECK(scores.drift_score == doctest::Approx(signals.drift));
}

TEST_CASE("classifier never yields an empty focus_state") {
    // focus_state lands in a TEXT NOT NULL column that happily accepts "", and recap()'s
    // `CASE WHEN focus_state = 'DEEP_FOCUS'` silently drops such rows, so an empty state is
    // invisible corruption rather than a loud failure. Whatever the backend does, predict()
    // must always name a state.
    struct Guard {
        ~Guard() { OnnxModel::instance().reset_for_tests(); }
    } guard;
    OnnxModel::instance().reset_for_tests();

    Classifier clf;
    auto valid = [](const std::string& s) {
        return s == "DISTRACTED" || s == "PSEUDO_PRODUCTIVE" || s == "PRODUCTIVE" ||
               s == "DEEP_FOCUS";
    };

    FeatureVector idle;  // all-zero features: the degenerate input
    CHECK(valid(clf.predict(idle, FocusMode::Normal).focus_state));

    FeatureVector busy;
    busy.keystroke_count() = 120.0;
    busy.keystroke_rate() = 4.0;
    busy.is_ide() = 1.0;
    for (const auto mode : {FocusMode::Deep, FocusMode::Normal, FocusMode::Recovery}) {
        CHECK(valid(clf.predict(busy, mode).focus_state));
    }
}
