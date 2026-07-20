import { useSummaryReport } from "./useSummaryReport";

const formatDuration = (seconds: number) => {
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
};

export function SummaryCard() {
  const { exportSummary, report, setWindow, status, window } = useSummaryReport();

  return (
    <section className="card insights-card">
      <div className="card-header">
        <h2>Summary</h2>
        <select aria-label="Summary window" value={window} onChange={(event) => setWindow(event.target.value as "day" | "week")}>
          <option value="day">Today</option>
          <option value="week">This week</option>
        </select>
      </div>
      <div className="insight-tiles">
        <div className="insight-tile"><p className="insight-tile-value">{formatDuration(report.focusSeconds)}</p><p className="insight-tile-label">Focus time</p></div>
        <div className="insight-tile"><p className="insight-tile-value">{report.sessionCount}</p><p className="insight-tile-label">Sessions</p></div>
        <div className="insight-tile"><p className="insight-tile-value">{Math.round(report.avgFocusScore)}</p><p className="insight-tile-label">Avg focus</p></div>
        <div className="insight-tile"><p className="insight-tile-value">{report.longestFocusStreak}</p><p className="insight-tile-label">Best streak</p></div>
      </div>
      <p className="helper-text">
        {report.topContextApp ? `Most common context: ${report.topContextApp}.` : "No context leader yet."}
        {` ${Math.round(report.distractedFraction * 100)}% of predictions were distracted.`}
      </p>
      <div className="button-row">
        <button className="secondary-button" onClick={() => void exportSummary()}>Export summary</button>
      </div>
      {status ? <p className="helper-text">{status}</p> : null}
    </section>
  );
}
