// ROADMAP 11.2 — property tests for the numeric core.
//
// `features.cpp` and `classifier.cpp` are pure functions over a feature vector, and until now
// they were covered only by example-based cases: a hand-built vector, a hand-checked answer.
// That style verifies the cases someone thought of, which is the same blind spot behind 7.1
// (the tests never seeded past the cap) and 5.2 (a `focus_state` outside the four labels
// shipped). Both would have been caught mechanically by an assertion that holds for *every*
// input rather than for the chosen ones.
//
// Two deliberate choices about how this is written.
//
// **The seed is fixed.** A property test that draws fresh randomness each run turns a real
// defect into an intermittent one and trains everybody to re-run it — the exact failure mode
// 11.1 just finished untangling in the capture layer. A fixed seed means the same 5,000
// vectors every run on every host, so a failure is reproducible on the machine that saw it
// *and* on the machine that has to fix it. The cost is that the corpus is fixed too; widening
// it is a deliberate edit, which is the right place for that decision.
//
// **Failures print the counterexample.** An invariant violation is worthless if you cannot see
// which vector caused it, so every check reports the iteration and the inputs that matter.
#include "doctest_wrapper.hpp"

#include <array>
#include <random>
#include <sstream>
#include <string>

#include "engine/classifier.hpp"
#include "engine/classifier_tuning.hpp"
#include "engine/features.hpp"
#include "types.hpp"

using namespace snapback;

namespace {

constexpr std::uint64_t kSeed = 0x5EEDC0FFEE1234ULL;
constexpr int kIterations = 5000;

const std::array<FocusMode, 3> kModes = {FocusMode::Deep, FocusMode::Normal,
                                         FocusMode::Recovery};

// Draws a feature vector spanning both plausible and adversarial values.
//
// The adversarial half matters more than the plausible half. Feature extraction is supposed to
// keep these in sane ranges, but the classifier is a separate module and its guarantees must
// not depend on its caller behaving — a stored feature snapshot replayed from an older build,
// or a future extractor change, can hand it anything. So roughly one draw in eight is extreme:
// negative, enormous, or zero.
class FeatureGenerator {
public:
    explicit FeatureGenerator(std::uint64_t seed) : rng_(seed) {}

    FeatureVector next() {
        FeatureVector f;
        for (std::size_t i = 0; i < f.values.size(); ++i) f.values[i] = draw();

        // Context flags are read as `> 0.5` booleans; give them a fair chance of being set
        // rather than relying on the numeric draw to land above the threshold.
        f.is_browser() = flag();
        f.is_ide() = flag();
        f.is_communication() = flag();
        f.is_entertainment() = flag();
        f.is_productivity() = flag();

        f.app_name = pick({"Cursor", "Chrome", "Slack", "YouTube", "", "Terminal"});
        f.window_title = pick({"state.cpp - Snapback", "watch later", "", "inbox (42)"});
        return f;
    }

    // Same generator, but a vector whose values are all plausible. Used where a property is
    // only claimed to hold on well-formed input.
    FeatureVector next_plausible() {
        FeatureVector f = next();
        for (auto& value : f.values) {
            if (value < 0.0 || value > 1e4) value = uniform_(rng_) * 10.0;
        }
        return f;
    }

    double uniform() { return uniform_(rng_); }

    std::string describe(const FeatureVector& f) const {
        std::ostringstream out;
        out << "app=" << f.app_name << " switches30=" << f.context_switches_30s()
            << " switches5m=" << f.context_switches_5min()
            << " uniqueApps=" << f.unique_apps_5min() << " keystrokes=" << f.keystroke_count()
            << " intervalStd=" << f.keystroke_interval_std()
            << " timeInApp=" << f.time_in_current_app() << " idle=" << f.idle_time_30s()
            << " momentum=" << f.focus_momentum();
        return out.str();
    }

private:
    double draw() {
        const double roll = uniform_(rng_);
        if (roll < 0.05) return -uniform_(rng_) * 1000.0;   // negative
        if (roll < 0.10) return uniform_(rng_) * 1e6;       // enormous
        if (roll < 0.125) return 0.0;                       // exactly zero
        return uniform_(rng_) * 300.0;                      // plausible
    }

    double flag() { return uniform_(rng_) < 0.5 ? 1.0 : 0.0; }

    std::string pick(std::initializer_list<const char*> options) {
        const auto index = static_cast<std::size_t>(uniform_(rng_) * options.size());
        return *(options.begin() + std::min(index, options.size() - 1));
    }

    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};
};

bool is_known_state(const std::string& state) {
    for (const char* label : tuning::kStateLabels) {
        if (state == label) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("classifier scores stay inside their declared ranges for any feature vector") {
    // The ranges every consumer already assumes. `focus_score` is rendered as a percentage,
    // `distraction_risk` is compared against risk_threshold(), and both are written to storage
    // and re-read by analytics — an out-of-range value corrupts a chart rather than throwing.
    FeatureGenerator generator(kSeed);
    Classifier classifier;

    for (int i = 0; i < kIterations; ++i) {
        const auto features = generator.next();
        for (const auto mode : kModes) {
            const auto scores = classifier.predict(features, mode);
            const auto why = [&] {
                std::ostringstream out;
                out << "iteration " << i << " mode " << static_cast<int>(mode) << ": "
                    << generator.describe(features);
                return out.str();
            };

            CHECK_MESSAGE(scores.focus_score >= 0.0, why());
            CHECK_MESSAGE(scores.focus_score <= 100.0, why());
            CHECK_MESSAGE(scores.distraction_risk >= 0.0, why());
            CHECK_MESSAGE(scores.distraction_risk <= 1.0, why());
            CHECK_MESSAGE(scores.thrash_score >= 0.0, why());
            CHECK_MESSAGE(scores.thrash_score <= 1.0, why());
            CHECK_MESSAGE(scores.drift_score >= 0.0, why());
            CHECK_MESSAGE(scores.drift_score <= 1.0, why());
            CHECK_MESSAGE(scores.goal_alignment >= 0.0, why());
            CHECK_MESSAGE(scores.goal_alignment <= 1.0, why());
        }
    }
}

TEST_CASE("focus_state is always one of the four known labels") {
    // This one has been violated in production: 5.2 shipped a state outside the set. Consumers
    // switch on the string — the UI colours it, storage counts DISTRACTED rows, and
    // infer_session_label maps it — so an unknown label degrades silently rather than loudly.
    FeatureGenerator generator(kSeed);
    Classifier classifier;

    for (int i = 0; i < kIterations; ++i) {
        const auto features = generator.next();
        for (const auto mode : kModes) {
            const auto scores = classifier.predict(features, mode);
            CHECK_MESSAGE(is_known_state(scores.focus_state),
                          "iteration " << i << " produced '" << scores.focus_state
                                       << "' from " << generator.describe(features));
        }
    }
}

TEST_CASE("the classifier is a pure function of its inputs") {
    // Determinism is what makes every other property here meaningful: a failing counterexample
    // must reproduce. It is also a real contract — the engine tick re-predicts continuously and
    // a prediction that drifts on identical input would show as flicker in the UI.
    FeatureGenerator generator(kSeed);
    Classifier classifier;

    for (int i = 0; i < 500; ++i) {
        const auto features = generator.next();
        const auto first = classifier.predict(features, FocusMode::Normal);
        const auto second = classifier.predict(features, FocusMode::Normal);

        CHECK(first.focus_score == second.focus_score);
        CHECK(first.distraction_risk == second.distraction_risk);
        CHECK(first.focus_state == second.focus_state);
        CHECK(first.thrash_score == second.thrash_score);
        CHECK(first.drift_score == second.drift_score);
    }
}

TEST_CASE("thrash never falls when context switching rises") {
    // The one monotonicity the heuristic genuinely claims: thrash_score is a convex combination
    // of three non-decreasing terms, so adding switches cannot reduce it. Stated as a property
    // because 7.15's static_asserts pin that the weights sum to 1 but nothing pinned the
    // *direction* — a sign flip on one weight would still sum correctly.
    FeatureGenerator generator(kSeed);
    Classifier classifier;

    for (int i = 0; i < 500; ++i) {
        auto features = generator.next_plausible();
        features.context_switches_30s() = 0.0;
        features.context_switches_5min() = 0.0;

        double previous = -1.0;
        for (const double switches : {0.0, 1.0, 2.0, 4.0, 8.0, 32.0}) {
            features.context_switches_30s() = switches;
            features.context_switches_5min() = switches * 2.0;
            const auto scores = classifier.predict(features, FocusMode::Normal);
            CHECK_MESSAGE(scores.thrash_score >= previous,
                          "iteration " << i << " switches=" << switches << " thrash fell from "
                                       << previous << " to " << scores.thrash_score);
            previous = scores.thrash_score;
        }
    }
}

TEST_CASE("scores_from_probabilities normalises any non-negative distribution") {
    // The ONNX entry point. A model's raw outputs are not guaranteed normalised, and this is
    // the seam that has to cope: focus_score is a probability-weighted average of the four
    // class levels, so it must land inside their span no matter what magnitudes arrive.
    FeatureGenerator generator(kSeed);

    for (int i = 0; i < 2000; ++i) {
        std::array<double, 4> probabilities{};
        for (auto& value : probabilities) value = generator.uniform() * 1000.0;

        const auto scores = scores_from_probabilities(probabilities, 0.0, 0.0, 0.5);
        CHECK(scores.focus_score >= tuning::kFocusLevels.front());
        CHECK(scores.focus_score <= tuning::kFocusLevels.back());
        CHECK(is_known_state(scores.focus_state));
    }
}

TEST_CASE("an all-zero distribution falls back to a uniform one") {
    // A model that outputs all zeros must not divide by zero or pick a class arbitrarily.
    // Uniform over four classes puts focus_score at the midpoint of the level span.
    const auto scores = scores_from_probabilities({0.0, 0.0, 0.0, 0.0}, 0.0, 0.0, 0.5);
    CHECK(scores.focus_score == doctest::Approx(62.5));
    CHECK(is_known_state(scores.focus_state));
}

TEST_CASE("guardrails force DISTRACTED whenever any of their conditions fires") {
    // The half of the guardrail contract that is unambiguous: risk at or above the mode's
    // threshold, thrash at or above the bar, or a personal Block rule each mean DISTRACTED
    // regardless of what the model believed.
    FeatureGenerator generator(kSeed);

    for (int i = 0; i < 2000; ++i) {
        std::array<double, 4> probabilities{};
        for (auto& value : probabilities) value = generator.uniform();
        const auto before = scores_from_probabilities(probabilities, 0.0, 0.0, 0.5);

        const double thrash = generator.uniform();
        const double drift = generator.uniform();
        const bool blocked = generator.uniform() < 0.5;

        for (const auto mode : kModes) {
            const auto after = apply_focus_guardrails(before, thrash, drift, blocked, mode);
            CHECK(is_known_state(after.focus_state));

            const bool must_be_distracted = before.distraction_risk >= risk_threshold(mode) ||
                                            thrash >= tuning::policy::kThrashDistracted ||
                                            blocked;
            if (must_be_distracted) {
                CHECK_MESSAGE(after.focus_state == "DISTRACTED",
                              "iteration " << i << ": risk=" << before.distraction_risk
                                           << " thrash=" << thrash << " blocked=" << blocked
                                           << " produced " << after.focus_state);
            }
        }
    }
}

TEST_CASE("the drift rule currently upgrades DISTRACTED to PSEUDO_PRODUCTIVE") {
    // CHARACTERIZATION TEST — this pins behaviour that is probably wrong. See ROADMAP 7.18,
    // which this test found.
    //
    // The property originally written here was the obvious one: guardrails are policy layered
    // on the model, and policy should only ever move a state *toward* distraction. Policy that
    // upgrades would let a rule make a distracted user look better, which is the opposite of
    // what a guardrail is for. It failed on 246 of 188,502 assertions.
    //
    // The cause is that the drift branch excludes only DEEP_FOCUS:
    //
    //     } else if (drift >= kDriftPseudo && scores.focus_state != "DEEP_FOCUS") {
    //         scores.focus_state = "PSEUDO_PRODUCTIVE";
    //
    // so a row the model called DISTRACTED — but whose risk sits under the mode threshold —
    // gets softened to PSEUDO_PRODUCTIVE, which scores 50 instead of 25.
    //
    // It is pinned rather than fixed because changing it changes the meaning of every stored
    // prediction and is a product call, not an obvious defect: see the head of this file's
    // ROADMAP tier on decisions mistaken for bugs. When 7.18 is settled this test fails, which
    // is the intended way to find it.
    auto before = scores_from_probabilities({0.30, 0.25, 0.25, 0.20}, 0.0, 0.0, 0.5);
    REQUIRE(before.focus_state == "DISTRACTED");
    REQUIRE(before.distraction_risk < risk_threshold(FocusMode::Normal));

    const auto after = apply_focus_guardrails(before, 0.10, 0.60, false, FocusMode::Normal);
    CHECK(after.focus_state == "PSEUDO_PRODUCTIVE");

    // DEEP_FOCUS is explicitly protected from the same rule, which is what makes the omission
    // of DISTRACTED look like an oversight rather than a considered asymmetry.
    auto deep = scores_from_probabilities({0.10, 0.15, 0.25, 0.50}, 0.0, 0.0, 0.5);
    REQUIRE(deep.focus_state == "DEEP_FOCUS");
    CHECK(apply_focus_guardrails(deep, 0.10, 0.60, false, FocusMode::Normal).focus_state ==
          "DEEP_FOCUS");
}
