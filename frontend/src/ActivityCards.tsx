import {
  focusStateLabel,
  formatPercent,
  formatScore,
  formatTime,
  riskLabel,
  riskLevel,
  type AppRuleKind,
  type AppRuleRecord,
  type ContextSnapshot,
  type PredictionRecord,
} from "./api";
import { getAppRuleForName, ruleKindLabel } from "./useAppRules";

type ContextTimelineProps = {
  appRules?: AppRuleRecord[];
  contextTimeline: ContextSnapshot[];
  onCreateAppRule?: (appName: string, kind: AppRuleKind) => void | Promise<void>;
};

export function PredictionHistoryCard({
  predictionHistory,
}: {
  predictionHistory: PredictionRecord[];
}) {
  return (
    <section className="card history-card">
      <div className="card-header">
        <h2>Recent Predictions</h2>
        <span className="pill">live · recent</span>
      </div>
      <ul className="history-list">
        {predictionHistory.length === 0 ? (
          <li className="history-empty">No predictions yet.</li>
        ) : (
          predictionHistory.map((entry) => (
            <li
              key={`${entry.timestampMs}-${entry.sessionId}-${entry.focusScore}`}
              className="history-item"
            >
              <div>
                <p className="history-time">{formatTime(entry.timestampMs)}</p>
                <p className="history-session">{focusStateLabel(entry.focusState)}</p>
              </div>
              <div className="history-metrics">
                <span className="history-score">
                  <span className="visually-hidden">Focus score </span>
                  {formatScore(entry.focusScore)}
                </span>
                <span
                  className={`history-risk risk-${riskLevel(entry.distractionRisk)}`}
                  title={riskLabel(entry.distractionRisk)}
                >
                  <span className="visually-hidden">Distraction risk </span>
                  {formatPercent(entry.distractionRisk)}
                  <span className="visually-hidden">
                    , {riskLabel(entry.distractionRisk).toLowerCase()}
                  </span>
                </span>
              </div>
            </li>
          ))
        )}
      </ul>
    </section>
  );
}

export function ContextTimeline({
  appRules,
  contextTimeline,
  onCreateAppRule,
}: ContextTimelineProps) {
  return (
    <section className="timeline-card">
      <div className="card-header">
        <h4>Context Timeline</h4>
      </div>
      <p className="helper-text">
        Where you were working during this session — apps, files, and parsed summaries.
      </p>
      <ol className="timeline-list">
        {contextTimeline.length === 0 ? (
          <li className="timeline-empty">No context snapshots yet.</li>
        ) : (
          contextTimeline.map((entry, index) => {
            const rule = appRules ? getAppRuleForName(appRules, entry.appName) : undefined;
            return (
              <li key={`${entry.timestampMs}-${entry.appName}-${index}`} className="timeline-item">
                <div className="timeline-marker" aria-hidden="true" />
                <div className="timeline-body">
                  <div className="timeline-header-row">
                    <p className="timeline-time">{formatTime(entry.timestampMs)}</p>
                    {rule && (
                      <span className={`rules-badge rules-badge-${rule.ruleType}`}>
                        {ruleKindLabel(rule.ruleType)}
                      </span>
                    )}
                  </div>
                  <p className="timeline-summary">{entry.summary || entry.windowTitle}</p>
                  <div className="timeline-meta-row">
                    <p className="timeline-meta">
                      {entry.appName}
                      {entry.fileHint ? ` · ${entry.fileHint}` : ""}
                      {entry.projectHint ? ` · ${entry.projectHint}` : ""}
                    </p>
                    {!rule && onCreateAppRule && entry.appName && (
                      <div className="timeline-quick-rules">
                        <button
                          type="button"
                          className="mini-action-button allow-btn"
                          title={`Always treat "${entry.appName}" as Productive`}
                          onClick={() => void onCreateAppRule(entry.appName, "allow")}
                        >
                          + Allow
                        </button>
                        <button
                          type="button"
                          className="mini-action-button block-btn"
                          title={`Always treat "${entry.appName}" as Distracting`}
                          onClick={() => void onCreateAppRule(entry.appName, "block")}
                        >
                          + Block
                        </button>
                      </div>
                    )}
                  </div>
                </div>
              </li>
            );
          })
        )}
      </ol>
    </section>
  );
}
