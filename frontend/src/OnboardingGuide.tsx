// Roadmap 2.12. The visible half of the guided continuation.
//
// It renders on Now, above the cockpit, because every step except the last is performed there.
// It has no Next button by design: the only controls are Skip and, when something is broken, a
// hand-off to the surface that owns the problem. Everything else advances because the user did
// the thing.
//
// ADR-0003's Now density: this is a strip, not a card that pushes Start below the fold.

import { memo } from "react";

import {
  ONBOARDING_COPY,
  onboardingProgress,
  type OnboardingFailure,
  type OnboardingStep,
} from "./onboardingJourney";

type Props = {
  step: OnboardingStep;
  /** Non-null when something is blocking progress and the guide must hand off. */
  failure: OnboardingFailure | null;
  onSkip: () => void;
  onRecover: () => void;
};

export const OnboardingGuide = memo(function OnboardingGuide({
  step,
  failure,
  onSkip,
  onRecover,
}: Props) {
  const { index, total } = onboardingProgress(step);
  const copy = ONBOARDING_COPY[step];

  return (
    <section className="onboarding-guide" aria-labelledby="onboarding-guide-title">
      <div className="onboarding-guide-row">
        <h2 id="onboarding-guide-title">Getting started</h2>
        <span className="pill">
          Step {index} of {total}
        </span>
        {/*
          `aria-live` because the heading changes underneath the user without any click of
          theirs — that is the whole design, and a silent swap is exactly the case screen-reader
          users would otherwise miss.
        */}
        <div role="status" aria-live="polite">
          <p className="onboarding-step-title">{copy.title}</p>
        </div>
        <button type="button" className="link-button" onClick={onSkip}>
          Skip the walkthrough
        </button>
      </div>

      {failure && (
        <div className="notice notice-untracked" role="alert">
          <p>{failure.message}</p>
          <button type="button" className="link-button" onClick={onRecover}>
            {failure.actionLabel}
          </button>
        </div>
      )}
    </section>
  );
});
