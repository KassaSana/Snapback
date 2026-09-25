import { memo } from "react";
import { formatFocusStretch } from "./focusStreak";

import { focusStateLabel, formatScore, type FocusLabel, type SessionRecap } from "./api";
import { SessionReflectionCard } from "./SessionReflectionCard";

type SessionReviewCardsProps = {
  autoLabel: FocusLabel | null;
  handleLabel: (label: FocusLabel, source?: "manual" | "hotkey" | "survey" | "auto") => void | Promise<void>;
  handleSkipSurvey: () => void;
  recap: SessionRecap | null;
  surveyPending: boolean;
  // Roadmap 2.14. Shown alongside the check-in at the end of a session, and closed by either
  // saving or skipping.
  reflectionPending: boolean;
  reflectionSaved: boolean;
  handleSaveReflection: (done: string | null, nextStep: string | null) => void | Promise<void>;
  handleSkipReflection: () => void;
};

export const SessionReviewCards = memo(function SessionReviewCards({
  autoLabel,
  handleLabel,
  handleSkipSurvey,
  recap,
  surveyPending,
  reflectionPending,
  reflectionSaved,
  handleSaveReflection,
  handleSkipReflection,
}: SessionReviewCardsProps) {
  return (
    <>
      {surveyPending && recap ? (
        <section className="card survey-card">
          <div className="card-header">
            <h2>Session Check-in</h2>
            <span className="pill">end of session</span>
          </div>
          <p className="helper-text">
            {autoLabel
              ? `Automatic label: ${focusStateLabel(autoLabel)}. Choose a different label if this feels wrong.`
              : "Automatic label unavailable. Choose a label, or skip this check-in."}
          </p>
          <div className="button-row feedback-row">
            <button className="secondary-button" onClick={() => void handleLabel("DEEP_FOCUS", "survey")}>
              Deep
            </button>
            <button className="secondary-button" onClick={() => void handleLabel("PRODUCTIVE", "survey")}>
              Focused
            </button>
            <button
              className="secondary-button"
              onClick={() => void handleLabel("PSEUDO_PRODUCTIVE", "survey")}
            >
              Drift
            </button>
            <button className="secondary-button" onClick={() => void handleLabel("DISTRACTED", "survey")}>
              Distracted
            </button>
            <button className="ghost-button" onClick={handleSkipSurvey}>
              {autoLabel ? `Keep ${focusStateLabel(autoLabel)}` : "Skip check-in"}
            </button>
          </div>
        </section>
      ) : null}

      {reflectionPending && recap ? (
        <SessionReflectionCard
          onSave={handleSaveReflection}
          onSkip={handleSkipReflection}
          saved={reflectionSaved}
        />
      ) : null}

      {recap ? (
        <section className="card recap-card">
          <div className="card-header">
            <h2>Session Recap</h2>
            <span className="pill">summary</span>
          </div>
          <div className="meta">
            <div>
              {/*
                Attended time leads when we have it (Roadmap 7.23 / ADR-0005): "you were here
                for 40 of the 95 minutes this session was open" is the honest headline, and
                elapsed alone used to report a session left running overnight as a night of
                focus. Sessions recorded before attended time existed have activeSecs === null
                and fall back to elapsed rather than claiming zero.
              */}
              <p className="meta-label">{recap.activeSecs === null ? "Duration" : "Attended"}</p>
              <p className="meta-value">
                {formatFocusStretch(recap.activeSecs ?? recap.durationSecs)}
              </p>
              {recap.activeSecs !== null && recap.activeSecs < recap.durationSecs && (
                <p className="meta-sub">of {formatFocusStretch(recap.durationSecs)} open</p>
              )}
            </div>
            <div>
              <p className="meta-label">Avg focus</p>
              <p className="meta-value">{formatScore(recap.avgFocusScore)}</p>
            </div>
            <div>
              <p className="meta-label">Deep work</p>
              <p className="meta-value">{recap.deepFocusPct.toFixed(0)}%</p>
            </div>
            <div>
              <p className="meta-label">Snapbacks</p>
              <p className="meta-value">{recap.snapbackCount}</p>
            </div>
            <div>
              <p className="meta-label">Distraction spikes</p>
              <p className="meta-value">{recap.thrashSpikes}</p>
            </div>
          </div>
        </section>
      ) : null}
    </>
  );
});
