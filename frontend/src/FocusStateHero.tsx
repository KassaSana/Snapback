import {
  explainPrediction,
  focusStateLabel,
  formatPercentCoarse,
  formatScoreCoarse,
  formatTime,
  type FocusLabel,
  type PredictionRecord,
} from "./api";
import { VerdictFeedback } from "./VerdictFeedback";

// The Now surface's primary element (ADR-0003).
//
// Leads with the focus *state* rather than the score: the score's scale is an open
// decision (Roadmap 5.3, 5.4, 1.2, 7.7), and a layout built around a hero number would be
// rebuilt when that lands. A state word survives a rescaling.
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
  prediction: PredictionRecord | null;
  riskClass: string;
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
  prediction,
  riskClass,
  sessionActive,
  snapbackNote,
}: Props) {
  const waiting = !prediction;
  const { reasons, caveat } = explainPrediction(prediction, goal);

  return (
    <section className="card hero-card" aria-labelledby="focus-state-heading">
      <h2 id="focus-state-heading" className="hero-eyebrow">
        Focus state
      </h2>

      <p className={`hero-state hero-state-${riskClass}`}>
        <span className={`hero-dot hero-dot-${riskClass}`} aria-hidden="true" />
        {waiting ? "Waiting for signal" : focusStateLabel(prediction?.focusState ?? null)}
      </p>

      {waiting ? (
        <p className="hero-secondary" aria-live="polite">
          No prediction yet — capture is warming up.
        </p>
      ) : (
        <>
          {reasons.length > 0 ? (
            <p className="hero-because" aria-live="polite">
              <span className="hero-because-label">because</span>
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
            {formatTime(prediction?.timestamp ?? null)}
          </p>

          {caveat ? <p className="hero-caveat">{caveat}</p> : null}
        </>
      )}

      {hyperfocusNote ? <p className="helper-text alert">{hyperfocusNote}</p> : null}
      {snapbackNote ? (
        <p className="helper-text snapback">
          {snapbackNote}{" "}
          <button className="link-button" onClick={onDismissSnapback}>
            Dismiss
          </button>
        </p>
      ) : null}

      {waiting ? null : (
        <VerdictFeedback
          disabled={!sessionActive}
          onConfirm={onConfirmVerdict}
          onCorrect={onCorrectVerdict}
          predictedState={prediction?.focusState ?? null}
          status={labelStatus}
        />
      )}
    </section>
  );
}
