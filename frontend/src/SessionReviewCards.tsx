import { memo } from "react";
import { formatFocusStretch } from "./focusStreak";

import { focusStateLabel, formatScore, sessionCheckInLabel, type FocusLabel, type SessionRecap } from "./api";
import { SessionReflectionCard } from "./SessionReflectionCard";

type SessionReviewCardsProps = {
  autoLabel: FocusLabel | null;
  handleLabel: (label: FocusLabel, source?: "manual" | "hotkey" | "survey" | "auto") => void | Promise<void>;
  handleSkipSurvey: () => void | Promise<void>;
  handleChangeSessionRating?: () => void;
  labelPending?: boolean;
  labelStatus?: string | null;
  labelStatusWarning?: boolean;
  savedSessionRating?: FocusLabel | null;
  recap: SessionRecap | null;
  surveyPending: boolean;
  // Shown with the check-in; closed by saving or skipping.
  reflectionPending: boolean;
  reflectionSaved: boolean;
  handleSaveReflection: (done: string | null, nextStep: string | null) => void | Promise<void>;
  handleSkipReflection: () => void;
};

export const SessionReviewCards = memo(function SessionReviewCards({
  autoLabel,
  handleLabel,
  handleSkipSurvey,
  handleChangeSessionRating,
  labelPending = false,
  labelStatus = null,
  labelStatusWarning = false,
  savedSessionRating = null,
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
              : recap.sampleCount === 0
                ? "No signal this session — no automatic label recorded. You can add your own rating or skip this check-in."
                : "Automatic label unavailable. Choose a label, or skip this check-in."}
          </p>
          <div className="button-row feedback-row">
            <button className="secondary-button" disabled={labelPending} onClick={() => void handleLabel("DEEP_FOCUS", "survey")}>
              Deep
            </button>
            <button className="secondary-button" disabled={labelPending} onClick={() => void handleLabel("PRODUCTIVE", "survey")}>
              Focused
            </button>
            <button
              className="secondary-button" disabled={labelPending}
              onClick={() => void handleLabel("PSEUDO_PRODUCTIVE", "survey")}
            >
              Drift
            </button>
            <button className="secondary-button" disabled={labelPending} onClick={() => void handleLabel("DISTRACTED", "survey")}>
              Distracted
            </button>
            <button className="ghost-button" disabled={labelPending} onClick={() => void handleSkipSurvey()}>
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
          {!surveyPending && savedSessionRating && handleChangeSessionRating ? (
            <p role="status" className="helper-text saved-session-rating">
              Saved: {sessionCheckInLabel(savedSessionRating)}
              {" — "}
              <button className="link-button" type="button" onClick={handleChangeSessionRating}>
                Change
              </button>
            </p>
          ) : labelStatus && (!surveyPending || labelStatusWarning) ? (
            <p role={labelStatusWarning ? "alert" : "status"} className={`helper-text${labelStatusWarning ? " alert" : ""}`}>
              {labelStatus}
            </p>
          ) : null}
          <div className="meta">
            <div>
              {/*
                Attended time leads when known (ADR-0005). Older sessions have activeSecs ===
                null and fall back to elapsed rather than claiming zero.
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
              <p className="meta-value">{recap.sampleCount === 0 ? "—" : formatScore(recap.avgFocusScore)}</p>
              {recap.sampleCount === 0 ? <p className="meta-sub">No predictions recorded</p> : null}
            </div>
            <div>
              <p className="meta-label">Deep work</p>
              <p className="meta-value">{recap.sampleCount === 0 ? "—" : `${recap.deepFocusPct.toFixed(0)}%`}</p>
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
