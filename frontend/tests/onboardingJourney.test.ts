import assert from "node:assert/strict";

import {
  ONBOARDING_COPY,
  ONBOARDING_DONE_KEY,
  ONBOARDING_STEPS,
  clearOnboardingComplete,
  currentOnboardingStep,
  onboardingFailure,
  onboardingProgress,
  readOnboardingComplete,
  shouldShowOnboarding,
  writeOnboardingComplete,
  type OnboardingState,
} from "../src/onboardingJourney";

import { MemoryStorage } from "./memoryStorage";

const state = (overrides: Partial<OnboardingState> = {}): OnboardingState => ({
  captureReady: true,
  goalEntered: false,
  sessionActive: false,
  predictionSeen: false,
  feedbackGiven: false,
  sessionCompleted: false,
  recapSeen: false,
  ...overrides,
});

// The journey teaches the product loop the item names, in that order.
assert.deepEqual(
  [...ONBOARDING_STEPS],
  ["goal", "start", "verdict", "correct", "stop", "review"],
);

// Every step is explained. A step with a title and no reason is a Next button with extra words.
for (const step of ONBOARDING_STEPS) {
  assert.ok(ONBOARDING_COPY[step]?.title.length > 0, `${step} has a title`);
  assert.ok(ONBOARDING_COPY[step]?.detail.length > 0, `${step} has a detail`);
}

// ---------------------------------------------------------------------------
// Step derivation. THE RULE: the step is read from app state, never advanced by a click.
// ---------------------------------------------------------------------------

// Nothing runs before 1.1's finish line — a walkthrough of reading verdicts is nonsense while
// the OS is still refusing to let us see the active window.
assert.equal(currentOnboardingStep(state({ captureReady: false })), null);
assert.equal(
  currentOnboardingStep(state({ captureReady: false, goalEntered: true })),
  null,
);

// The ordinary walk, one step at a time.
assert.equal(currentOnboardingStep(state()), "goal");
assert.equal(currentOnboardingStep(state({ goalEntered: true })), "start");
assert.equal(currentOnboardingStep(state({ goalEntered: true, sessionActive: true })), "verdict");
assert.equal(
  currentOnboardingStep(state({ goalEntered: true, sessionActive: true, predictionSeen: true })),
  "correct",
);
assert.equal(
  currentOnboardingStep(
    state({
      goalEntered: true,
      sessionActive: true,
      predictionSeen: true,
      feedbackGiven: true,
    }),
  ),
  "stop",
);
assert.equal(currentOnboardingStep(state({ sessionCompleted: true })), "review");
assert.equal(currentOnboardingStep(state({ sessionCompleted: true, recapSeen: true })), null);

// THE failure this derivation exists to prevent: a user who ran ahead must not be walked back.
// Someone who started a session without reading the guide is on "verdict", not "goal" — the
// counter-based version of this says "now type a goal" over a running session.
assert.equal(currentOnboardingStep(state({ sessionActive: true })), "verdict");
assert.equal(
  currentOnboardingStep(state({ sessionActive: true, predictionSeen: true, feedbackGiven: true })),
  "stop",
);
// And someone who completed a whole session before ever opening the guide skips to the payoff.
assert.equal(
  currentOnboardingStep(state({ goalEntered: false, sessionCompleted: true })),
  "review",
);

// Stopping mid-journey does not strand the user on a step about a session that has ended.
assert.equal(
  currentOnboardingStep(state({ sessionActive: false, sessionCompleted: true, predictionSeen: false })),
  "review",
);

// Progress reads as "n of 6" and is complete when there is no step left.
assert.deepEqual(onboardingProgress("goal"), { index: 1, total: 6 });
assert.deepEqual(onboardingProgress("correct"), { index: 4, total: 6 });
assert.deepEqual(onboardingProgress("review"), { index: 6, total: 6 });
assert.deepEqual(onboardingProgress(null), { index: 6, total: 6 });

// ---------------------------------------------------------------------------
// Failures hand off rather than talking over the problem.
// ---------------------------------------------------------------------------

assert.equal(onboardingFailure({ captureFailed: false, privateMode: false }), null);

{
  const failed = onboardingFailure({ captureFailed: true, privateMode: false });
  assert.ok(failed);
  assert.ok(failed.actionLabel.toLowerCase().includes("permission"));

  const priv = onboardingFailure({ captureFailed: false, privateMode: true });
  assert.ok(priv);
  assert.ok(priv.message.toLowerCase().includes("private mode"));
  assert.ok(priv.actionLabel.toLowerCase().includes("privacy"));

  // Capture being down outranks private mode: one is broken, the other is a choice.
  const both = onboardingFailure({ captureFailed: true, privateMode: true });
  assert.deepEqual(both, failed);
}

// ---------------------------------------------------------------------------
// Persistence. Local only — the item forbids implying an analytics service.
// ---------------------------------------------------------------------------

{
  const storage = new MemoryStorage();
  assert.equal(readOnboardingComplete(storage), false);

  writeOnboardingComplete(storage);
  assert.equal(readOnboardingComplete(storage), true);
  assert.equal(storage.getItem(ONBOARDING_DONE_KEY), "true");

  // Resumable from Help: clearing puts the user back at the start, and because the guide only
  // observes, replaying it creates no second session and no duplicate label.
  clearOnboardingComplete(storage);
  assert.equal(readOnboardingComplete(storage), false);
  assert.equal(currentOnboardingStep(state()), "goal");
}

// Absent or hostile storage is survivable in every direction.
assert.equal(readOnboardingComplete(null), false);
writeOnboardingComplete(null);
clearOnboardingComplete(null);

// ---------------------------------------------------------------------------
// Visibility.
// ---------------------------------------------------------------------------

assert.equal(shouldShowOnboarding({ captureReady: true, completed: false, step: "goal" }), true);
// Skipped or finished stays gone...
assert.equal(shouldShowOnboarding({ captureReady: true, completed: true, step: "goal" }), false);
// ...as does "no step left" and "capture is not ready yet".
assert.equal(shouldShowOnboarding({ captureReady: true, completed: false, step: null }), false);
assert.equal(shouldShowOnboarding({ captureReady: false, completed: false, step: "goal" }), false);

console.log("onboardingJourney.test.ts passed");
