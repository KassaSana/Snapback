import {
  focusStateLabel,
  formatPercentCoarse,
  formatScoreCoarse,
  formatTime,
  type PredictionRecord,
} from "./api";

// The Now surface's primary element (ADR-0003).
//
// Leads with the focus *state* rather than the score, deliberately: the score's scale is
// still an open decision (Roadmap 5.3, 5.4, 1.2, 7.7), and a layout built around a hero
// number would have to be rebuilt when that lands. A state word survives a rescaling.
// The numbers stay, demoted and rounded — `71` rather than `71.2`, because one decimal
// implies precision this model has not earned.
//
// Snapback and hyperfocus notes are the exception to "state first": they are the moments
// the whole product exists for, so they render as prominent callouts.

type Props = {
  hyperfocusNote: string | null;
  onDismissSnapback: () => void;
  prediction: PredictionRecord | null;
  riskClass: string;
  snapbackNote: string | null;
};

export function FocusStateHero({
  hyperfocusNote,
  onDismissSnapback,
  prediction,
  riskClass,
  snapbackNote,
}: Props) {
  const state = focusStateLabel(prediction?.focusState ?? null);
  const waiting = !prediction;

  return (
    <section className="card hero-card" aria-labelledby="focus-state-heading">
      <h2 id="focus-state-heading" className="hero-eyebrow">
        Focus state
      </h2>

      <p className={`hero-state hero-state-${riskClass}`}>
        <span className={`hero-dot hero-dot-${riskClass}`} aria-hidden="true" />
        {waiting ? "Waiting for signal" : state}
      </p>

      {/* aria-live so a screen reader hears the state change without re-reading the page. */}
      <p className="hero-secondary" aria-live="polite">
        {waiting ? (
          "No prediction yet — capture is warming up."
        ) : (
          <>
            focus {formatScoreCoarse(prediction?.focusScore ?? null)}
            <span className="hero-sep" aria-hidden="true">
              ·
            </span>
            risk {formatPercentCoarse(prediction?.distractionRisk ?? null)}
            <span className="hero-sep" aria-hidden="true">
              ·
            </span>
            {formatTime(prediction?.timestamp ?? null)}
          </>
        )}
      </p>

      {hyperfocusNote ? <p className="helper-text alert">{hyperfocusNote}</p> : null}
      {snapbackNote ? (
        <p className="helper-text snapback">
          {snapbackNote}{" "}
          <button className="link-button" onClick={onDismissSnapback}>
            Dismiss
          </button>
        </p>
      ) : null}
    </section>
  );
}
