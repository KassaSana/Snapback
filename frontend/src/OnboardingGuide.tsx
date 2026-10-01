// The guided continuation, as a strip on Now. No Next button: steps advance when the user does
// them; the only controls are Skip and a hand-off when something is broken.

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
          {step === "review" ? "Finish walkthrough" : "Skip the walkthrough"}
        </button>
      </div>

      <p className="helper-text">{copy.detail}</p>
      {step === "capture" && !failure ? (
        <button type="button" className="link-button" onClick={onRecover}>Open privacy settings</button>
      ) : null}
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
