import {
  focusStateLabel,
  formatPercent,
  formatScore,
  formatTime,
  riskLevel,
  type ContextSnapshot,
  type PredictionRecord,
} from "./api";

type ActivityCardsProps = {
  contextTimeline: ContextSnapshot[];
  historyLimit: number;
  predictionHistory: PredictionRecord[];
  refreshContextTimeline: (sessionId: string) => void | Promise<void>;
  sessionId: string | null;
};

export function ActivityCards({
  contextTimeline,
  historyLimit,
  predictionHistory,
  refreshContextTimeline,
  sessionId,
}: ActivityCardsProps) {
  return (
    <>
      <section className="card history-card">
        <div className="card-header">
          <h2>Recent Predictions</h2>
          <span className="pill">latest {historyLimit}</span>
        </div>
        <ul className="history-list">
          {predictionHistory.length === 0 ? (
            <li className="history-empty">No predictions yet.</li>
          ) : (
            predictionHistory.map((entry) => (
              <li
                key={`${entry.timestamp}-${entry.sessionId}-${entry.focusScore}`}
                className="history-item"
              >
                <div>
                  <p className="history-time">{formatTime(entry.timestamp)}</p>
                  <p className="history-session">{focusStateLabel(entry.focusState)}</p>
                </div>
                <div className="history-metrics">
                  <span className="history-score">{formatScore(entry.focusScore)}</span>
                  <span className={`history-risk risk-${riskLevel(entry.distractionRisk)}`}>
                    {formatPercent(entry.distractionRisk)}
                  </span>
                </div>
              </li>
            ))
          )}
        </ul>
      </section>

      <section className="card timeline-card">
        <div className="card-header">
          <h2>Context Timeline</h2>
          <span className="pill">session trail</span>
        </div>
        <p className="helper-text">
          Where you were working during this session — apps, files, and parsed summaries.
        </p>
        {!sessionId ? (
          <p className="helper-text">Start a session to record context snapshots.</p>
        ) : (
          <>
            <button className="ghost-button" onClick={() => void refreshContextTimeline(sessionId)}>
              Refresh timeline
            </button>
            <ol className="timeline-list">
              {contextTimeline.length === 0 ? (
                <li className="timeline-empty">No context snapshots yet.</li>
              ) : (
                contextTimeline.map((entry, index) => (
                  <li
                    key={`${entry.timestamp}-${entry.appName}-${index}`}
                    className="timeline-item"
                  >
                    <div className="timeline-marker" aria-hidden="true" />
                    <div className="timeline-body">
                      <p className="timeline-time">{formatTime(entry.timestamp)}</p>
                      <p className="timeline-summary">{entry.summary || entry.windowTitle}</p>
                      <p className="timeline-meta">
                        {entry.appName}
                        {entry.fileHint ? ` · ${entry.fileHint}` : ""}
                        {entry.projectHint ? ` · ${entry.projectHint}` : ""}
                      </p>
                    </div>
                  </li>
                ))
              )}
            </ol>
          </>
        )}
      </section>
    </>
  );
}
