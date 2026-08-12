import { memo } from "react";
import { FOCUS_STRETCH_LABEL, formatFocusStretch } from "./focusStreak";

import type { SummaryReport } from "./api";

const formatDuration = (seconds: number) => {
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
};

type SummaryCardProps = {
  exportStatus: string | null;
  onExport: () => void;
  rangeLabel: string;
  report: SummaryReport;
};

export const SummaryCard = memo(function SummaryCard({
  exportStatus,
  onExport,
  rangeLabel,
  report,
}: SummaryCardProps) {
  const hasHistory = report.sampleCount > 0 || report.completedSessionCount > 0;

  return (
    <section className="card insights-card">
      <div className="card-header">
        <h2>Summary</h2>
        <span className="pill">{rangeLabel}</span>
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
          No summary data for this range yet. Complete a session to build your report.
        </p>
      )}
      <div className="button-row">
        <button
          className="secondary-button"
          disabled={!hasHistory}
          onClick={onExport}
        >
          Export summary
        </button>
      </div>
      {exportStatus ? <p className="helper-text">{exportStatus}</p> : null}
    </section>
  );
});
