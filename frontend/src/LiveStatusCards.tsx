import {
  focusStateLabel,
  formatPercent,
  formatScore,
  formatTime,
  type PredictionRecord,
} from "./api";

type LiveStatusCardsProps = {
  hyperfocusNote: string | null;
  onDismissSnapback: () => void;
  prediction: PredictionRecord | null;
  riskBadgeLabel: string;
  riskClass: string;
  signals: string[];
  snapbackNote: string | null;
};

export function LiveStatusCards({
  hyperfocusNote,
  onDismissSnapback,
  prediction,
  riskBadgeLabel,
  riskClass,
  signals,
  snapbackNote,
}: LiveStatusCardsProps) {
  return (
    <>
      <section className="card live-card">
        <div className="card-header">
          <h2>Live Prediction</h2>
          <span className={`risk-badge risk-${riskClass}`}>{riskBadgeLabel}</span>
        </div>
        <div className="metrics">
          <div className="metric">
            <p className="metric-label">Focus score</p>
            <p className="metric-value">{formatScore(prediction?.focusScore ?? null)}</p>
          </div>
          <div className="metric">
            <p className="metric-label">Distraction risk</p>
            <p className="metric-value">{formatPercent(prediction?.distractionRisk ?? null)}</p>
          </div>
          <div className="metric">
            <p className="metric-label">State</p>
            <p className="metric-value state-value">
              {focusStateLabel(prediction?.focusState ?? null)}
            </p>
          </div>
        </div>
        <div className="meta">
          <div>
            <p className="meta-label">Last update</p>
            <p className="meta-value">{formatTime(prediction?.timestamp ?? null)}</p>
          </div>
          <div>
            <p className="meta-label">Session</p>
            <p className="meta-value">{prediction?.sessionId || "--"}</p>
          </div>
        </div>
      </section>

      <section className="card signals-card">
        <div className="card-header">
          <h2>Signals</h2>
          <span className="pill">rolling 30s</span>
        </div>
        <ul className="signal-list">
          {signals.map((signal, index) => (
            <li key={`${signal}-${index}`}>{signal}</li>
          ))}
        </ul>
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
    </>
  );
}
