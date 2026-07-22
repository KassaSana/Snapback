#include "doctest_wrapper.hpp"

#include "engine/focus_summary.hpp"

using namespace snapback;

namespace {
PredictionRecord pred(double score, const char* state) {
    PredictionRecord p;
    p.focus_score = score;
    p.focus_state = state;
    return p;
}
}  // namespace

TEST_CASE("summarize_predictions on empty input is zeroed") {
    const auto s = summarize_predictions({});
    CHECK(s.sample_count == 0);
    CHECK(s.avg_focus_score == doctest::Approx(0.0));
    CHECK(s.longest_focus_streak == 0);
}

TEST_CASE("summarize_predictions computes average, peak, and distracted fraction") {
    std::vector<PredictionRecord> preds{
        pred(80, "DEEP_FOCUS"),
        pred(40, "DISTRACTED"),
        pred(60, "PRODUCTIVE"),
        pred(100, "DEEP_FOCUS"),
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.sample_count == 4);
    CHECK(s.avg_focus_score == doctest::Approx(70.0));
    CHECK(s.peak_focus_score == doctest::Approx(100.0));
    CHECK(s.distracted_samples == 1);
    CHECK(s.distracted_fraction == doctest::Approx(0.25));
}

TEST_CASE("summarize_predictions finds the longest non-distracted streak") {
    std::vector<PredictionRecord> preds{
        pred(70, "PRODUCTIVE"),   // streak 1
        pred(70, "DEEP_FOCUS"),   // streak 2
        pred(30, "DISTRACTED"),   // break
        pred(70, "PRODUCTIVE"),   // streak 1
        pred(70, "PRODUCTIVE"),   // streak 2
        pred(70, "DEEP_FOCUS"),   // streak 3
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_streak == 3);
    CHECK(s.distracted_samples == 1);
}

TEST_CASE("summarize_predictions: all distracted -> zero streak, fraction 1") {
    std::vector<PredictionRecord> preds{pred(10, "DISTRACTED"), pred(20, "DISTRACTED")};
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_streak == 0);
    CHECK(s.distracted_fraction == doctest::Approx(1.0));
    CHECK(s.peak_focus_score == doctest::Approx(20.0));
}
