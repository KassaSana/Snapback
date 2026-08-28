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

// The Now surface's primary element (ADR-0003).
//
// Leads with the focus *state* rather than the score, and ADR-0004 made that the
// permanent contract rather than sequencing caution: the state is the policy verdict —
// the thing the app acts on — and the score is the model's opinion, demoted to the
// evidence line. The two may disagree on purpose.
//
// One exception, display-only: a quiet screen with no goal match is a guess from
// absence (classifier.cpp:deep_work_score). Printing "Deep work" there is unearned
// confidence. The stored verdict is left alone so snapbacks and streaks do not change
// meaning; only the word on this card becomes "Settled".
//
// The verdict is shown with its evidence, not on its own. An unexplained label is
// unfalsifiable — you cannot tell a good guess from a lucky one — and the classifier is
// hand-tuned thresholds that have never been evaluated against ground truth. Showing the
// reasoning makes a wrong answer visibly wrong, and the rating control turns that into a
// labelled example.

type Props = {
  goal: string | null;
  hyperfocusNote: string | null;
  labelStatus: string | null;
  onConfirmVerdict: () => void;
  onCorrectVerdict: (label: FocusLabel) => void;
  onDismissSnapback: () => void;
  onRestoreSnapbackTarget?: () => void;
  prediction: PredictionRecord | null;
  sessionActive: boolean;
  snapbackNote: string | null;
};

export function FocusStateHero({
  goal,
  hyperfocusNote,
  labelStatus,
  onConfirmVerdict,
  onCorrectVerdict,
  onDismissSnapback,
  onRestoreSnapbackTarget,
  prediction,
  sessionActive,
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

      {idle ? (
        <p className="hero-secondary" aria-live="polite">
          Start a session to see whether you are still on it.
        </p>
      ) : waiting ? (
        <p className="hero-secondary" aria-live="polite">
          No prediction yet — capture is warming up.
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
          onConfirm={onConfirmVerdict}
          onCorrect={onCorrectVerdict}
          predictedState={prediction?.focusState ?? null}
          status={labelStatus}
          uncertain={uncertain}
        />
      )}
    </section>
  );
}
