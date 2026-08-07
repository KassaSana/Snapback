#include "doctest_wrapper.hpp"

#include <string>

#include "engine/focus_summary.hpp"

using namespace snapback;

namespace {

// Roadmap 10.13. Timestamps are load-bearing now: the focused stretch is measured in seconds
// between neighbours, not counted in rows, so every fixture has to sit on a real clock.
PredictionRecord pred(double score, const char* state, const char* timestamp = nullptr) {
    PredictionRecord p;
    p.focus_score = score;
    p.focus_state = state;
    if (timestamp) p.timestamp = timestamp;
    return p;
}

std::string at(int minute, int second = 0) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "2026-08-06T10:%02d:%02dZ", minute, second);
    return buffer;
}

}  // namespace

TEST_CASE("summarize_predictions on empty input is zeroed") {
    const auto s = summarize_predictions({});
    CHECK(s.sample_count == 0);
    CHECK(s.avg_focus_score == doctest::Approx(0.0));
    CHECK(s.longest_focus_secs == 0);
}

TEST_CASE("summarize_predictions computes average, peak, and distracted fraction") {
    std::vector<PredictionRecord> preds{
        pred(80, "DEEP_FOCUS", at(0).c_str()),
        pred(40, "DISTRACTED", at(1).c_str()),
        pred(60, "PRODUCTIVE", at(2).c_str()),
        pred(100, "DEEP_FOCUS", at(3).c_str()),
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.sample_count == 4);
    CHECK(s.avg_focus_score == doctest::Approx(70.0));
    CHECK(s.peak_focus_score == doctest::Approx(100.0));
    CHECK(s.distracted_samples == 1);
    CHECK(s.distracted_fraction == doctest::Approx(0.25));
}

TEST_CASE("the focused stretch is measured in seconds, not counted in rows") {
    // Roadmap 10.13. The tile used to show a count of consecutive non-DISTRACTED prediction
    // rows under the label "Focus streak". Rows are not time.
    //
    // Two focused runs: 10 seconds of frequent samples, then 90 seconds of sparse ones. The
    // *row count* says the first run is longer (five samples against three); the clock says
    // the second is, by nine times. The clock is right, and it is what the label claims.
    std::vector<PredictionRecord> preds{
        pred(70, "PRODUCTIVE", at(0, 0).c_str()),
        pred(70, "PRODUCTIVE", at(0, 2).c_str()),
        pred(70, "DEEP_FOCUS", at(0, 4).c_str()),
        pred(70, "PRODUCTIVE", at(0, 7).c_str()),
        pred(70, "DEEP_FOCUS", at(0, 10).c_str()),  // run one: 10s across five samples
        pred(30, "DISTRACTED", at(0, 40).c_str()),  // break
        pred(70, "PRODUCTIVE", at(1, 0).c_str()),
        pred(70, "PRODUCTIVE", at(2, 0).c_str()),
        pred(70, "DEEP_FOCUS", at(2, 30).c_str()),  // run two: 90s across three samples
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_secs == 90);
    CHECK(s.distracted_samples == 1);
}

TEST_CASE("identical work at different typing cadence reports the same focused time") {
    // The defect stated plainly: under a row count, the user who types more scores higher for
    // the same hour of work. Two runs over the same 60 seconds, sampled twice and six times.
    std::vector<PredictionRecord> sparse{
        pred(70, "PRODUCTIVE", at(0, 0).c_str()),
        pred(70, "PRODUCTIVE", at(1, 0).c_str()),
    };
    std::vector<PredictionRecord> dense{
        pred(70, "PRODUCTIVE", at(0, 0).c_str()),
        pred(70, "PRODUCTIVE", at(0, 12).c_str()),
        pred(70, "PRODUCTIVE", at(0, 24).c_str()),
        pred(70, "PRODUCTIVE", at(0, 36).c_str()),
        pred(70, "PRODUCTIVE", at(0, 48).c_str()),
        pred(70, "PRODUCTIVE", at(1, 0).c_str()),
    };
    CHECK(summarize_predictions(sparse).longest_focus_secs == 60);
    CHECK(summarize_predictions(dense).longest_focus_secs == 60);
    // And the row counts that used to be shown differ by three times, which is the point.
    CHECK(sparse.size() * 3 == dense.size());
}

TEST_CASE("a gap longer than the bound breaks the stretch instead of being counted") {
    // Predictions stop entirely while the user is idle or in private mode, so a long gap is a
    // pause in the *user*, not in the data. Counting it would report walking away as focus.
    std::vector<PredictionRecord> preds{
        pred(70, "PRODUCTIVE", at(0, 0).c_str()),
        pred(70, "PRODUCTIVE", at(0, 30).c_str()),   // +30s, inside the bound
        pred(70, "PRODUCTIVE", at(10, 0).c_str()),   // a 9.5-minute hole: they left
        pred(70, "PRODUCTIVE", at(10, 20).c_str()),  // +20s, a new run
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_secs == 30);
    CHECK(s.sample_count == 4);  // the samples still count; only the run broke
}

TEST_CASE("a gap exactly at the bound still counts, and one second past it does not") {
    std::vector<PredictionRecord> at_bound{
        pred(70, "PRODUCTIVE", "2026-08-06T10:00:00Z"),
        pred(70, "PRODUCTIVE", "2026-08-06T10:02:00Z"),  // exactly kFocusRunGapSecs
    };
    CHECK(summarize_predictions(at_bound).longest_focus_secs ==
          static_cast<std::uint64_t>(kFocusRunGapSecs));

    std::vector<PredictionRecord> past_bound{
        pred(70, "PRODUCTIVE", "2026-08-06T10:00:00Z"),
        pred(70, "PRODUCTIVE", "2026-08-06T10:02:01Z"),
    };
    CHECK(summarize_predictions(past_bound).longest_focus_secs == 0);
}

TEST_CASE("a single focused sample is zero seconds, not one") {
    // One measurement has no duration. Reporting "1" here is exactly the row-count confusion
    // this replaced.
    std::vector<PredictionRecord> preds{pred(90, "DEEP_FOCUS", at(0).c_str())};
    CHECK(summarize_predictions(preds).longest_focus_secs == 0);
}

TEST_CASE("all distracted -> zero focused time, fraction 1") {
    std::vector<PredictionRecord> preds{pred(10, "DISTRACTED", at(0).c_str()),
                                        pred(20, "DISTRACTED", at(1).c_str())};
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_secs == 0);
    CHECK(s.distracted_fraction == doctest::Approx(1.0));
    CHECK(s.peak_focus_score == doctest::Approx(20.0));
}

TEST_CASE("a backwards clock cannot lengthen a focused stretch") {
    // DST and NTP corrections move wall time backwards. A negative interval must break the run
    // rather than subtract from it or, worse, be taken as a large positive by unsigned math.
    std::vector<PredictionRecord> preds{
        pred(70, "PRODUCTIVE", "2026-08-06T10:00:00Z"),
        pred(70, "PRODUCTIVE", "2026-08-06T10:00:30Z"),
        pred(70, "PRODUCTIVE", "2026-08-06T09:00:00Z"),  // the clock jumped back
        pred(70, "PRODUCTIVE", "2026-08-06T09:00:10Z"),
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_secs == 30);
}

TEST_CASE("an unparseable timestamp ends the stretch rather than being invented into it") {
    // Rows written before timestamps were reliable, or a corrupted value: it cannot be
    // measured against its neighbours, so it breaks the run instead of joining it at a guess.
    std::vector<PredictionRecord> preds{
        pred(70, "PRODUCTIVE", at(0, 0).c_str()),
        pred(70, "PRODUCTIVE", at(0, 40).c_str()),
        pred(70, "PRODUCTIVE", "not a timestamp"),
        pred(70, "PRODUCTIVE", at(1, 0).c_str()),
        pred(70, "PRODUCTIVE", at(1, 10).c_str()),
    };
    const auto s = summarize_predictions(preds);
    CHECK(s.longest_focus_secs == 40);
}
