#include <doctest/doctest.h>

#include "engine/confidence.hpp"

using namespace snapback;

TEST_CASE("distraction_confidence is 0 below threshold, ramps to 1 at max") {
    ConfidenceConfig cfg;  // threshold 60
    CHECK(distraction_confidence(0.0, cfg) == doctest::Approx(0.0));
    CHECK(distraction_confidence(60.0, cfg) == doctest::Approx(0.0));
    CHECK(distraction_confidence(80.0, cfg) == doctest::Approx(0.5));  // halfway 60->100
    CHECK(distraction_confidence(100.0, cfg) == doctest::Approx(1.0));
}

TEST_CASE("should_nag suppresses borderline low-confidence risk") {
    ConfidenceConfig cfg;  // threshold 60, min_confidence 0.25 -> needs risk >= 70
    CHECK_FALSE(should_nag(59.0, cfg));  // under threshold
    CHECK_FALSE(should_nag(65.0, cfg));  // over threshold but only 0.125 confident
    CHECK(should_nag(70.0, cfg));        // exactly 0.25 confident
    CHECK(should_nag(95.0, cfg));        // clearly distracted
}

TEST_CASE("should_nag respects a custom stricter config") {
    ConfidenceConfig strict{/*nag_threshold=*/50.0, /*min_confidence=*/0.8};
    // Needs risk >= 50 + 0.8*50 = 90.
    CHECK_FALSE(should_nag(85.0, strict));
    CHECK(should_nag(90.0, strict));
}

TEST_CASE("distraction_confidence handles a degenerate 100 threshold") {
    ConfidenceConfig cfg{100.0, 0.25};
    CHECK(distraction_confidence(99.0, cfg) == doctest::Approx(0.0));
    CHECK(distraction_confidence(100.0, cfg) == doctest::Approx(1.0));
}
