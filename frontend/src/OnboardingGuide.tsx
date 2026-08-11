// Roadmap 2.12. The visible half of the guided continuation.
//
// It renders on Now, above the cockpit, because every step except the last is performed there.
// It has no Next button by design: the only controls are Skip and, when something is broken, a
// hand-off to the surface that owns the problem. Everything else advances because the user did
// the thing.

import { memo } from "react";

import {
  ONBOARDING_COPY,
  ONBOARDING_STEPS,
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
  const currentIndex = ONBOARDING_STEPS.indexOf(step);

  return (
    <section className="card onboarding-guide" aria-labelledby="onboarding-guide-title">
      <div className="card-header">
        <h2 id="onboarding-guide-title">Getting started</h2>
        <span className="pill">
          Step {index} of {total}
        </span>
      </div>

      {/*
        `aria-live` because the heading changes underneath the user without any click of
        theirs — that is the whole design, and a silent swap is exactly the case screen-reader
        users would otherwise miss.
      */}
      <div role="status" aria-live="polite">
        <p className="onboarding-step-title">{copy.title}</p>
        <p className="helper-text">{copy.detail}</p>
      </div>

      {failure && (
        <div className="notice notice-untracked" role="alert">
          <p>{failure.message}</p>
          <button type="button" className="link-button" onClick={onRecover}>
            {failure.actionLabel}
          </button>
        </div>
      )}

      {/*
        Decorative. The current step is already announced by the live region above and counted
        by the pill beside the heading, so repeating all six titles here would make a screen
        reader recite the entire journey on every advance.
      */}
      <ol className="onboarding-track" aria-hidden="true">
        {ONBOARDING_STEPS.map((entry, position) => (
          <li
            key={entry}
            className={
              position < currentIndex
                ? "onboarding-dot onboarding-dot-done"
                : position === currentIndex
                  ? "onboarding-dot onboarding-dot-current"
                  : "onboarding-dot"
            }
          />
        ))}
      </ol>

      <button type="button" className="link-button" onClick={onSkip}>
        Skip the walkthrough
      </button>
    </section>
  );
});
