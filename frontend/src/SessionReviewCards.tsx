import { formatScore, type FocusLabel, type SessionRecap } from "./api";

type SessionReviewCardsProps = {
  handleLabel: (label: FocusLabel, source?: "manual" | "hotkey" | "survey" | "auto") => void | Promise<void>;
  handleSkipSurvey: () => void;
  recap: SessionRecap | null;
  surveyPending: boolean;
};

export function SessionReviewCards({
  handleLabel,
  handleSkipSurvey,
  recap,
  surveyPending,
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
            We saved an automatic label from your recap. Override it if your gut says different.
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
              Keep automatic label
            </button>
          </div>
        </section>
      ) : null}

      {recap ? (
        <section className="card recap-card">
          <div className="card-header">
            <h2>Session Recap</h2>
            <span className="pill">summary</span>
          </div>
          <div className="meta">
            <div>
              <p className="meta-label">Duration</p>
              <p className="meta-value">{Math.round(recap.durationSecs / 60)} min</p>
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
              <p className="meta-label">Thrash spikes</p>
              <p className="meta-value">{recap.thrashSpikes}</p>
            </div>
          </div>
        </section>
      ) : null}
    </>
  );
}
