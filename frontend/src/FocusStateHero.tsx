import {
  displayVerdict,
  explainPrediction,
  formatPercentCoarse,
  formatScoreCoarse,
  formatTime,
  type FocusLabel,
  type PredictionRecord,
} from "./api";
import { VerdictFeedback } from "./VerdictFeedback";

// The Now surface's primary element (ADR-0003). Leads with the focus state (the verdict) rather
// than the score (the model's opinion, ADR-0004), and shows the evidence beside it so a wrong
// answer looks wrong.
//
// Display-only exception: a quiet screen with no goal match is a guess from absence, so "Deep
// work" reads as "Settled" here; the stored verdict is unchanged.

type Props = {
  goal: string | null;
  hyperfocusNote: string | null;
  labelStatus: string | null;
  onCorrectVerdict: (label: FocusLabel) => void;
  onDismissSnapback: () => void;
  onRestoreSnapbackTarget?: () => void;
  prediction: PredictionRecord | null;
  sessionActive: boolean;
  captureDiagnosis?: string | null;
  recordingPaused?: boolean;
  onOpenPrivacy?: () => void;
  snapbackNote: string | null;
};

export function FocusStateHero({
  goal,
  hyperfocusNote,
  labelStatus,
  onCorrectVerdict,
  onDismissSnapback,
  onRestoreSnapbackTarget,
  prediction,
  sessionActive,
  captureDiagnosis = null,
  recordingPaused = false,
  onOpenPrivacy,
  snapbackNote,
}: Props) {
  const waiting = !prediction;
  const display = displayVerdict(prediction, goal);
  const { reasons, caveat, uncertain } = explainPrediction(prediction, goal);
  // Idle Now is a start screen. A leftover (or demo) prediction is not the session, so it
  // does not get the full verdict + scores + caveat — that is what made idle a second Review.
  const idle = !sessionActive;

  return (
    <section
      className={idle ? "card hero-card hero-card-idle" : "card hero-card"}
      aria-labelledby="focus-state-heading"
    >
      <h2 id="focus-state-heading" className="hero-eyebrow">
        Focus state
      </h2>

      <p className={`hero-state hero-state-${idle ? "unknown" : display.level}`}>
        <span
          className={`hero-dot${idle ? "" : ` hero-dot-${display.level}`}`}
          aria-hidden="true"
        />
        {idle ? "Ready" : display.label}
      </p>

      {captureDiagnosis && onOpenPrivacy ? (
        <div role="alert"><p>{captureDiagnosis}</p><button className="link-button" onClick={onOpenPrivacy}>Open privacy settings</button></div>
      ) : null}
      {recordingPaused && prediction ? <p className="helper-text">Recording paused — this is the last reading, held until recording resumes.</p> : null}
      {idle ? (
        <p className="hero-secondary" aria-live="polite">
          Start a session to see whether you are still on it.
        </p>
      ) : waiting ? (
        <p className="hero-secondary" aria-live="polite">
          {recordingPaused ? "Recording is paused. Readings resume with recording." : captureDiagnosis ?? "No reading yet — move the pointer, scroll, or press a key to confirm capture."}
        </p>
      ) : (
        <>
          {uncertain ? (
            <p className="hero-because" aria-live="polite">
              I cannot tell whether this is work.
            </p>
          ) : reasons.length > 0 ? (
            <p className="hero-because" aria-live="polite">
              <span className="hero-because-label">because </span>
              {reasons.map((reason, index) => (
                <span key={reason} className="hero-reason">
                  {index > 0 ? (
                    <span className="hero-sep" aria-hidden="true">
                      ·
                    </span>
                  ) : null}
                  {reason}
                </span>
              ))}
            </p>
          ) : null}

          <p className="hero-secondary">
            focus {formatScoreCoarse(prediction?.focusScore ?? null)}
            <span className="hero-sep" aria-hidden="true">
              ·
            </span>
            risk {formatPercentCoarse(prediction?.distractionRisk ?? null)}
            <span className="hero-sep" aria-hidden="true">
              ·
            </span>
            {formatTime(prediction?.timestampMs ?? null)}
          </p>

          {caveat ? <p className="hero-caveat">{caveat}</p> : null}
        </>
      )}

      {hyperfocusNote ? <p className="helper-text alert">{hyperfocusNote}</p> : null}
      {snapbackNote ? (
        <p className="helper-text snapback">
          {snapbackNote}{" "}
          {onRestoreSnapbackTarget ? (
            <>
              <button className="link-button" onClick={onRestoreSnapbackTarget}>
                Take me back
              </button>{" "}
              ·{" "}
            </>
          ) : null}
          <button className="link-button" onClick={onDismissSnapback}>
            Dismiss
          </button>
        </p>
      ) : null}

      {idle || waiting ? null : (
        <VerdictFeedback
          disabled={!sessionActive}
          onCorrect={onCorrectVerdict}
          predictedState={prediction?.focusState ?? null}
          status={labelStatus}
        />
      )}
    </section>
  );
}
