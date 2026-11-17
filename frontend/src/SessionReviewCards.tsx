import { memo } from "react";
import { formatFocusStretch } from "./focusStreak";

import { FOCUS_RATINGS, focusStateLabel, formatScore, type FocusLabel, type SessionRecap } from "./api";
import { isSessionFragment } from "./sessionFragment";
import { SessionReflectionCard } from "./SessionReflectionCard";

type SessionReviewCardsProps = {
  autoLabel: FocusLabel | null;
  handleLabel: (label: FocusLabel, source?: "manual" | "hotkey" | "survey" | "auto") => void | Promise<void>;
  handleSkipSurvey: () => void | Promise<void>;
  handleDiscardFragment?: () => void | Promise<void>;
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
  handleDiscardFragment,
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
  const fragment = recap ? isSessionFragment(recap) : false;

  return (
    <>
      {surveyPending && recap ? (
        <section className="card survey-card">
          <div className="card-header">
            <h2>Check-in</h2>
            {fragment ? <span className="pill">short session</span> : null}
          </div>
          <p className="helper-text">
            {fragment
              ? "This session was under a minute with no readings. Discard it, or keep it and rate."
              : autoLabel
                ? `Automatic label: ${focusStateLabel(autoLabel)}. Choose a different label if this feels wrong.`
                : recap.sampleCount === 0
                  ? "No activity was recorded, so there's no automatic label. Rate it yourself or skip."
                  : "Automatic label unavailable. Choose a label, or skip this check-in."}
          </p>
          <div className="button-row feedback-row">
            {fragment && handleDiscardFragment ? (
              <button
                className="secondary-button"
                disabled={labelPending}
                onClick={() => void handleDiscardFragment()}
              >
                Discard
              </button>
            ) : null}
            {FOCUS_RATINGS.map((label) => (
              <button
                key={label}
                className="secondary-button"
                disabled={labelPending}
                onClick={() => void handleLabel(label, "survey")}
              >
                {focusStateLabel(label)}
              </button>
            ))}
            <button className="ghost-button" disabled={labelPending} onClick={() => void handleSkipSurvey()}>
              {fragment ? "Keep & skip rating" : autoLabel ? `Keep ${focusStateLabel(autoLabel)}` : "Skip check-in"}
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
            <h2>Recap</h2>
          </div>
          {!surveyPending && savedSessionRating && handleChangeSessionRating ? (
            <p role="status" className="helper-text saved-session-rating">
              Saved: {focusStateLabel(savedSessionRating)}
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
              <p className="meta-label">Distracted moments</p>
              <p className="meta-value">{recap.thrashSpikes}</p>
            </div>
          </div>
        </section>
      ) : null}
    </>
  );
});
