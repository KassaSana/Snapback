// Tests the ONNX backend. resolve_model_path is exercised in every build; the actual
// load+run + heuristic-fallback path only when SNAPBACK_ONNX is on (fixtures/model.onnx).
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/classifier.hpp"
#include "engine/features.hpp"
#include "engine/onnx_model.hpp"

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

TEST_CASE("resolve_model_path prefers the app dir, then the training-export dir") {
    auto dir = make_temp_dir();
    CHECK(OnnxModel::resolve_model_path(dir) == std::nullopt);

    touch(dir / "exports" / "training" / "model.onnx");
    CHECK(OnnxModel::resolve_model_path(dir) == dir / "exports" / "training" / "model.onnx");

    touch(dir / "model.onnx");  // app-dir copy wins
    CHECK(OnnxModel::resolve_model_path(dir) == dir / "model.onnx");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
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
}
#endif
