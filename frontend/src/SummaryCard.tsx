import { memo } from "react";
import { FOCUS_STRETCH_LABEL, formatFocusStretch } from "./focusStreak";

import { useSummaryReport } from "./useSummaryReport";

const formatDuration = (seconds: number) => {
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
};

export const SummaryCard = memo(function SummaryCard() {
  const { exportSummary, report, setWindow, status, window } = useSummaryReport();
  // A just-started first session is counted before its first prediction. A completed
  // zero-prediction session is still real history (for example, capture permission failed),
  // so the backend reports that state separately.
  const hasHistory = report.sampleCount > 0 || report.completedSessionCount > 0;

  return (
    <section className="card insights-card">
      <div className="card-header">
        <h2>Summary</h2>
        <select aria-label="Summary window" value={window} onChange={(event) => setWindow(event.target.value as "day" | "week")}>
          <option value="day">Last 24 hours</option>
          <option value="week">Last 7 days</option>
        </select>
      </div>
      {hasHistory ? (
        <>
          <div className="insight-tiles">
            <div className="insight-tile"><p className="insight-tile-value">{formatDuration(report.focusSeconds)}</p><p className="insight-tile-label">Focus time</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{report.sessionCount}</p><p className="insight-tile-label">Sessions</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{Math.round(report.avgFocusScore)}</p><p className="insight-tile-label">Avg focus</p></div>
            <div className="insight-tile"><p className="insight-tile-value">{formatFocusStretch(report.longestFocusSecs)}</p><p className="insight-tile-label">{FOCUS_STRETCH_LABEL}</p></div>
          </div>
          <p className="helper-text">
            {report.topContextApp ? `Most common context: ${report.topContextApp}.` : "No context leader yet."}
            {` ${Math.round(report.distractedFraction * 100)}% of predictions were distracted.`}
          </p>
        </>
      ) : (
        <p className="helper-text">
          No summary data yet. Complete a session to unlock 24-hour and 7-day reports.
        </p>
      )}
      <div className="button-row">
        <button
          className="secondary-button"
          disabled={!hasHistory}
          onClick={() => void exportSummary()}
        >
          Export summary
        </button>
      </div>
      {status ? <p className="helper-text">{status}</p> : null}
    </section>
  );
});
